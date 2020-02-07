#include <set>
#include <algorithm>
#include "omp.h"
#include "Eigen/Sparse"
#include "finite_element_routine.hpp"
#include "static_analysis.hpp"

namespace statics_with_nonloc
{

// Небольшая структура, которая объединяет в себе номер узла и индекс переменной, который на ней задан.
struct node_info
{
    uint32_t              number : 31;
    enum component {X, Y} comp   :  1;

    node_info(uint64_t number, component comp = X) :
        number(static_cast<uint32_t>(number)), comp(comp) {}
    
    operator Eigen::SparseMatrix<double>::StorageIndex() const {
        return static_cast<Eigen::SparseMatrix<double>::StorageIndex>(number);
    }

    template<class Type>
    friend bool operator<(const node_info left, const Type right) {
        return left.number < right;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"

    friend bool operator<(const node_info left, const node_info right) {
        return *reinterpret_cast<const uint32_t*>(&left) < *reinterpret_cast<const uint32_t*>(&right);
    }

    friend bool operator!=(const node_info left, const node_info right) {
        return *reinterpret_cast<const uint32_t*>(&left) != *reinterpret_cast<const uint32_t*>(&right);
    }

#pragma GCC diagnostic pop
};

template<class Type>
static Type integrate_loc(const finite_element::element_2d_integrate_base<Type> *const e,
                          const node_info i, const node_info j, const matrix<Type> &jacobi_matrices, size_t shift,
                          const std::array<Type, 3> &coeff)
{
    Type integral = 0.;
    if(i.comp == node_info::X)
    {
        if(j.comp == node_info::X) // XX
        {
            for(size_t q = 0; q < e->qnodes_count(); ++q, ++shift)
                integral += (coeff[0] * ( e->qNxi(i, q)*jacobi_matrices(shift, 3) - e->qNeta(i, q)*jacobi_matrices(shift, 2)) *
                                        ( e->qNxi(j, q)*jacobi_matrices(shift, 3) - e->qNeta(j, q)*jacobi_matrices(shift, 2)) +
                             coeff[2] * (-e->qNxi(i, q)*jacobi_matrices(shift, 1) + e->qNeta(i, q)*jacobi_matrices(shift, 0)) *
                                        (-e->qNxi(j, q)*jacobi_matrices(shift, 1) + e->qNeta(j, q)*jacobi_matrices(shift, 0))) /
                            (jacobi_matrices(shift, 0)*jacobi_matrices(shift, 3) - jacobi_matrices(shift, 1)*jacobi_matrices(shift, 2)) * e->weight(q);
        }
        else // XY
        {
            for(size_t q = 0; q < e->qnodes_count(); ++q, ++shift)
                integral += (coeff[1] * (-e->qNxi(i, q)*jacobi_matrices(shift, 1) + e->qNeta(i, q)*jacobi_matrices(shift, 0)) *
                                        ( e->qNxi(j, q)*jacobi_matrices(shift, 3) - e->qNeta(j, q)*jacobi_matrices(shift, 2)) +
                             coeff[2] * ( e->qNxi(i, q)*jacobi_matrices(shift, 3) - e->qNeta(i, q)*jacobi_matrices(shift, 2)) *
                                        (-e->qNxi(j, q)*jacobi_matrices(shift, 1) + e->qNeta(j, q)*jacobi_matrices(shift, 0))) /
                            (jacobi_matrices(shift, 0)*jacobi_matrices(shift, 3) - jacobi_matrices(shift, 1)*jacobi_matrices(shift, 2)) * e->weight(q);
        }
    }
    else
    {
        if(j.comp == node_info::X) //YX
        {
            for(size_t q = 0; q < e->qnodes_count(); ++q, ++shift)
                integral += (coeff[1] * ( e->qNxi(i, q)*jacobi_matrices(shift, 3) - e->qNeta(i, q)*jacobi_matrices(shift, 2)) *
                                        (-e->qNxi(j, q)*jacobi_matrices(shift, 1) + e->qNeta(j, q)*jacobi_matrices(shift, 0)) +
                             coeff[2] * (-e->qNxi(i, q)*jacobi_matrices(shift, 1) + e->qNeta(i, q)*jacobi_matrices(shift, 0)) *
                                        ( e->qNxi(j, q)*jacobi_matrices(shift, 3) - e->qNeta(j, q)*jacobi_matrices(shift, 2))) /
                            (jacobi_matrices(shift, 0)*jacobi_matrices(shift, 3) - jacobi_matrices(shift, 1)*jacobi_matrices(shift, 2)) * e->weight(q);
        }
        else // YY
        {
            for(size_t q = 0; q < e->qnodes_count(); ++q, ++shift)
                integral += (coeff[0] * (-e->qNxi(i, q)*jacobi_matrices(shift, 1) + e->qNeta(i, q)*jacobi_matrices(shift, 0)) *
                                        (-e->qNxi(j, q)*jacobi_matrices(shift, 1) + e->qNeta(j, q)*jacobi_matrices(shift, 0)) +
                             coeff[2] * ( e->qNxi(i, q)*jacobi_matrices(shift, 3) - e->qNeta(i, q)*jacobi_matrices(shift, 2)) *
                                        ( e->qNxi(j, q)*jacobi_matrices(shift, 3) - e->qNeta(j, q)*jacobi_matrices(shift, 2))) /
                            (jacobi_matrices(shift, 0)*jacobi_matrices(shift, 3) - jacobi_matrices(shift, 1)*jacobi_matrices(shift, 2)) * e->weight(q);
        }
    }   
    return integral;
}

template<class Type>
static Type integrate_nonloc(const finite_element::element_2d_integrate_base<Type> *const eL,
                             const finite_element::element_2d_integrate_base<Type> *const eNL,
                             const node_info iL, const node_info jNL, size_t shiftL, size_t shiftNL,
                             const matrix<Type> &coords, const matrix<Type> &jacobi_matrices,
                             const std::function<Type(Type, Type, Type, Type)> &influence_fun,
                             const std::array<Type, 3> &coeff)
{
    const size_t sub = shiftNL;
    Type integral = 0.;
    if(iL.comp == node_info::X)
    {
        if(jNL.comp == node_info::X) // XX
        {
            Type int_with_weight_x = 0., int_with_weight_y = 0., finit = 0.;
            for(size_t qL = 0; qL < eL->qnodes_count(); ++qL, ++shiftL)
            {
                int_with_weight_x = 0.;
                int_with_weight_y = 0.;
                for(size_t qNL = 0, shiftNL = sub; qNL < eNL->qnodes_count(); ++qNL, ++shiftNL)
                {
                    finit = eNL->weight(qNL) * influence_fun(coords(shiftL, 0), coords(shiftNL, 0), coords(shiftL, 1), coords(shiftNL, 1));
                    int_with_weight_x += finit * ( eNL->qNxi(jNL, qNL) * jacobi_matrices(shiftNL, 3) - eNL->qNeta(jNL, qNL) * jacobi_matrices(shiftNL, 2));
                    int_with_weight_y += finit * (-eNL->qNxi(jNL, qNL) * jacobi_matrices(shiftNL, 1) + eNL->qNeta(jNL, qNL) * jacobi_matrices(shiftNL, 0));
                }
                integral += eL->weight(qL) *
                            (coeff[0] * int_with_weight_x * ( eL->qNxi(iL, qL) * jacobi_matrices(shiftL, 3) - eL->qNeta(iL, qL) * jacobi_matrices(shiftL, 2)) +
                             coeff[2] * int_with_weight_y * (-eL->qNxi(iL, qL) * jacobi_matrices(shiftL, 1) + eL->qNeta(iL, qL) * jacobi_matrices(shiftL, 0)));                
            }
        }
        else // XY
        {
            Type int_with_weight_x = 0., int_with_weight_y = 0., finit = 0.;
            for(size_t qL = 0; qL < eL->qnodes_count(); ++qL, ++shiftL)
            {
                int_with_weight_x = 0.;
                int_with_weight_y = 0.;
                for(size_t qNL = 0, shiftNL = sub; qNL < eNL->qnodes_count(); ++qNL, ++shiftNL)
                {
                    finit = eNL->weight(qNL) * influence_fun(coords(shiftL, 0), coords(shiftNL, 0), coords(shiftL, 1), coords(shiftNL, 1));
                    int_with_weight_x += finit * ( eNL->qNxi(jNL, qNL) * jacobi_matrices(shiftNL, 1) - eNL->qNeta(jNL, qNL) * jacobi_matrices(shiftNL, 0));
                    int_with_weight_y += finit * (-eNL->qNxi(jNL, qNL) * jacobi_matrices(shiftNL, 3) + eNL->qNeta(jNL, qNL) * jacobi_matrices(shiftNL, 2));
                }
                integral += eL->weight(qL) *
                            (coeff[1] * int_with_weight_x * ( eL->qNxi(iL, qL) * jacobi_matrices(shiftL, 3) - eL->qNeta(iL, qL) * jacobi_matrices(shiftL, 2)) +
                             coeff[2] * int_with_weight_y * (-eL->qNxi(iL, qL) * jacobi_matrices(shiftL, 1) + eL->qNeta(iL, qL) * jacobi_matrices(shiftL, 0)));                
            }
        }
    }
    else
    {
        if(jNL.comp == node_info::X) //YX
        {
            Type int_with_weight_x = 0., int_with_weight_y = 0., finit = 0.;
            for(size_t qL = 0; qL < eL->qnodes_count(); ++qL, ++shiftL)
            {
                int_with_weight_x = 0.;
                int_with_weight_y = 0.;
                for(size_t qNL = 0, shiftNL = sub; qNL < eNL->qnodes_count(); ++qNL, ++shiftNL)
                {
                    finit = eNL->weight(qNL) * influence_fun(coords(shiftL, 0), coords(shiftNL, 0), coords(shiftL, 1), coords(shiftNL, 1));
                    int_with_weight_x += finit * ( eNL->qNxi(jNL, qNL) * jacobi_matrices(shiftNL, 3) - eNL->qNeta(jNL, qNL) * jacobi_matrices(shiftNL, 2));
                    int_with_weight_y += finit * (-eNL->qNxi(jNL, qNL) * jacobi_matrices(shiftNL, 1) + eNL->qNeta(jNL, qNL) * jacobi_matrices(shiftNL, 0));
                }
                integral += eL->weight(qL) *
                            (coeff[1] * int_with_weight_x * ( eL->qNxi(iL, qL) * jacobi_matrices(shiftL, 1) - eL->qNeta(iL, qL) * jacobi_matrices(shiftL, 0)) +
                             coeff[2] * int_with_weight_y * (-eL->qNxi(iL, qL) * jacobi_matrices(shiftL, 3) + eL->qNeta(iL, qL) * jacobi_matrices(shiftL, 2)));                
            }
        }
        else // YY
        {
            Type int_with_weight_x = 0., int_with_weight_y = 0., finit = 0.;
            for(size_t qL = 0; qL < eL->qnodes_count(); ++qL, ++shiftL)
            {
                int_with_weight_x = 0.;
                int_with_weight_y = 0.;
                for(size_t qNL = 0, shiftNL = sub; qNL < eNL->qnodes_count(); ++qNL, ++shiftNL)
                {
                    finit = eNL->weight(qNL) * influence_fun(coords(shiftL, 0), coords(shiftNL, 0), coords(shiftL, 1), coords(shiftNL, 1));
                    int_with_weight_x += finit * ( eNL->qNxi(jNL, qNL) * jacobi_matrices(shiftNL, 1) - eNL->qNeta(jNL, qNL) * jacobi_matrices(shiftNL, 0));
                    int_with_weight_y += finit * (-eNL->qNxi(jNL, qNL) * jacobi_matrices(shiftNL, 3) + eNL->qNeta(jNL, qNL) * jacobi_matrices(shiftNL, 2));
                }
                integral += eL->weight(qL) *
                            (coeff[0] * int_with_weight_x * ( eL->qNxi(iL, qL) * jacobi_matrices(shiftL, 1) - eL->qNeta(iL, qL) * jacobi_matrices(shiftL, 0)) +
                             coeff[2] * int_with_weight_y * (-eL->qNxi(iL, qL) * jacobi_matrices(shiftL, 3) + eL->qNeta(iL, qL) * jacobi_matrices(shiftL, 2)));                
            }
        }
    }   
    return integral;
}

template<class Type>
static Type integrate_force_bound(const finite_element::element_1d_integrate_base<Type> *be, const size_t i,
                                  const matrix<Type> &coords, const matrix<Type> &jacobi_matrices, 
                                  const std::function<Type(Type, Type)> &fun)
{
    Type integral = 0.;
    for(size_t q = 0; q < be->qnodes_count(); ++q)
        integral += fun(coords(q, 0), coords(q, 1)) * be->weight(q) * be->qN(i, q) *
                    sqrt(jacobi_matrices(q, 0)*jacobi_matrices(q, 0) + jacobi_matrices(q, 1)*jacobi_matrices(q, 1));
    return integral;
}

static std::set<node_info> kinematic_nodes_set(const mesh_2d<double> &mesh,
                                               const std::vector<std::tuple<boundary_type, std::function<double(double, double)>,
                                                                            boundary_type, std::function<double(double, double)>>> &bounds_cond)
{
    std::set<node_info> kinematic_nodes;
    for(size_t b = 0; b < bounds_cond.size(); ++b)
    {
        if(std::get<0>(bounds_cond[b]) == boundary_type::TRANSLATION)
            for(auto node = mesh.boundary(b).cbegin(); node != mesh.boundary(b).cend(); ++node)
                kinematic_nodes.insert(node_info(*node, node_info::X));

        if(std::get<2>(bounds_cond[b]) == boundary_type::TRANSLATION)
            for(auto node = mesh.boundary(b).cbegin(); node != mesh.boundary(b).cend(); ++node)
                kinematic_nodes.insert(node_info(*node, node_info::Y));
    }
    return kinematic_nodes;
}

static std::vector<std::vector<uint32_t>> kinematic_nodes_vectors(const mesh_2d<double> &mesh,
                                              const std::vector<std::tuple<boundary_type, std::function<double(double, double)>,
                                                                           boundary_type, std::function<double(double, double)>>> &bounds_cond)
{
    std::vector<std::vector<uint32_t>> kinematic_nodes(bounds_cond.size());
    for(size_t b = 0; b < bounds_cond.size(); ++b)
        if(std::get<0>(bounds_cond[b]) == boundary_type::TRANSLATION ||
           std::get<2>(bounds_cond[b]) == boundary_type::TRANSLATION)
            for(auto [node, k] = std::make_tuple(mesh.boundary(b).cbegin(), static_cast<size_t>(0)); node != mesh.boundary(b).cend(); ++node)
            {
                for(k = 0; k < kinematic_nodes.size(); ++k)
                    if(std::find(kinematic_nodes[k].cbegin(), kinematic_nodes[k].cend(), *node) != kinematic_nodes[k].cend())
                        break;
                if(k == kinematic_nodes.size())
                    kinematic_nodes[b].push_back(*node);
            }
    return kinematic_nodes;
}

static void translation(const mesh_2d<double> &mesh, const Eigen::SparseMatrix<double> &K_bound, Eigen::VectorXd &f,
                        const std::function<double(double, double)> &boundaryFun, const size_t node)
{
    double temp = boundaryFun(mesh.coord(node >> 1, 0), mesh.coord(node >> 1, 1));
    for(typename Eigen::SparseMatrix<double>::InnerIterator it(K_bound, node); it; ++it)
        f[it.row()] -= temp * it.value();
}

static void boundary_condition(const mesh_2d<double> &mesh, const std::vector<std::vector<uint32_t>> &temperature_nodes,
                               const std::vector<std::tuple<boundary_type, std::function<double(double, double)>,
                                                            boundary_type, std::function<double(double, double)>>> &bounds_cond,
                               const double tau, const Eigen::SparseMatrix<double> &K_bound, Eigen::VectorXd &f)
{
    const finite_element::element_1d_integrate_base<double> *be = nullptr;
    matrix<double> coords, jacobi_matrices;
    for(size_t b = 0; b < bounds_cond.size(); ++b)
    {
        if(std::get<0>(bounds_cond[b]) == boundary_type::FORCE)
            for(size_t el = 0; el < mesh.boundary(b).rows(); ++el)
            {
                be = mesh.element_1d(mesh.elements_on_bound_types(b)[el]);
                approx_jacobi_matrices_bound(mesh, be, b, el, jacobi_matrices);
                approx_quad_nodes_coord_bound(mesh, be, b, el, coords);
                for(size_t i = 0; i < mesh.boundary(b).cols(el); ++i)
                    f[mesh.boundary(b)(el, i)] += tau*integrate_force_bound(be, i, coords, jacobi_matrices, std::get<1>(bounds_cond[b]));
            }

        if(std::get<2>(bounds_cond[b]) == boundary_type::FORCE)
            for(size_t el = 0; el < mesh.boundary(b).rows(); ++el)
            {
                be = mesh.element_1d(mesh.elements_on_bound_types(b)[el]);
                approx_jacobi_matrices_bound(mesh, be, b, el, jacobi_matrices);
                approx_quad_nodes_coord_bound(mesh, be, b, el, coords);
                for(size_t i = 0; i < mesh.boundary(b).cols(el); ++i)
                    f[mesh.boundary(b)(el, i)] += tau*integrate_force_bound(be, i, coords, jacobi_matrices, std::get<3>(bounds_cond[b]));
            }
    }

    for(size_t b = 0; b < temperature_nodes.size(); ++b)
    {
        if(std::get<0>(bounds_cond[b]) == boundary_type::TRANSLATION)
            for(auto node : temperature_nodes[b])
                translation(mesh, K_bound, f, std::get<1>(bounds_cond[b]), 2*node);

        if(std::get<2>(bounds_cond[b]) == boundary_type::TRANSLATION)
            for(auto node : temperature_nodes[b])
                translation(mesh, K_bound, f, std::get<3>(bounds_cond[b]), 2*node+1);
    }

    for(size_t b = 0; b < temperature_nodes.size(); ++b)
    {
        if(std::get<0>(bounds_cond[b]) == boundary_type::TRANSLATION)
            for(auto node : temperature_nodes[b])
                f[2*node]   = std::get<1>(bounds_cond[b])(mesh.coord(node, 0), mesh.coord(node, 1));

        if(std::get<2>(bounds_cond[b]) == boundary_type::TRANSLATION)
            for(auto node : temperature_nodes[b])
                f[2*node+1] = std::get<3>(bounds_cond[b])(mesh.coord(node, 0), mesh.coord(node, 1));
    }
}

static std::array<std::vector<uint32_t>, 4>
    mesh_analysis(const mesh_2d<double> &mesh, const std::set<node_info> &kinematic_nodes, const bool nonlocal)
{
    std::vector<uint32_t> shifts_loc(mesh.elements_count()+1, 0), shifts_bound_loc(mesh.elements_count()+1, 0),
                          shifts_nonloc, shifts_bound_nonloc;

    const std::function<void(node_info, node_info, size_t)>
        counter_loc = [&mesh, &kinematic_nodes, &shifts_loc, &shifts_bound_loc]
                      (node_info node_i, node_info node_j, size_t el)
                      {
                          node_info glob_i = {mesh.node_number(el, node_i.number), node_i.comp},
                                    glob_j = {mesh.node_number(el, node_j.number), node_j.comp};
                          if(2 * glob_i.number + glob_i.comp >= 2 * glob_j.number + glob_j.comp)
                          {
                              if(kinematic_nodes.find(glob_i) == kinematic_nodes.cend() &&
                                 kinematic_nodes.find(glob_j) == kinematic_nodes.cend())
                                  ++shifts_loc[el+1];
                              else if(glob_i != glob_j)
                                  ++shifts_bound_loc[el+1];
                          }
                      };

    mesh_run_loc(mesh, [&counter_loc](size_t i, size_t j, size_t el)
                       {
                           counter_loc({i, node_info::X}, {j, node_info::X}, el);
                           counter_loc({i, node_info::X}, {j, node_info::Y}, el);
                           counter_loc({i, node_info::Y}, {j, node_info::X}, el);
                           counter_loc({i, node_info::Y}, {j, node_info::Y}, el);
                       });

    shifts_loc[0] = kinematic_nodes.size();
    for(size_t i = 1; i < shifts_loc.size(); ++i)
    {
        shifts_loc[i] += shifts_loc[i-1];
        shifts_bound_loc[i] += shifts_bound_loc[i-1];
    }

    if(nonlocal)
    {
        shifts_nonloc.resize(mesh.elements_count()+1, 0);
        shifts_bound_nonloc.resize(mesh.elements_count()+1, 0);

        const std::function<void(node_info, node_info, size_t, size_t)>
        counter_nonloc = [&mesh, &kinematic_nodes, &shifts_nonloc, &shifts_bound_nonloc]
                         (node_info node_iL, node_info node_jNL, size_t elL, size_t elNL)
                         {
                             node_info glob_iL  = {mesh.node_number(elL,  node_iL.number),  node_iL.comp},
                                       glob_jNL = {mesh.node_number(elNL, node_jNL.number), node_jNL.comp};
                             if(2 * glob_iL.number + glob_iL.comp >= 2 * glob_jNL.number + glob_jNL.comp)
                             {
                                 if(kinematic_nodes.find(glob_iL)  == kinematic_nodes.cend() &&
                                    kinematic_nodes.find(glob_jNL) == kinematic_nodes.cend())
                                     ++shifts_nonloc[elL+1];
                                 else if(glob_iL != glob_jNL)
                                     ++shifts_bound_nonloc[elL+1];
                             }
                         };

        mesh_run_nonloc(mesh, [&counter_nonloc](size_t iL, size_t jNL, size_t elL, size_t elNL)
                              {
                                  counter_nonloc({iL, node_info::X}, {jNL, node_info::X}, elL, elNL);
                                  counter_nonloc({iL, node_info::X}, {jNL, node_info::Y}, elL, elNL);
                                  counter_nonloc({iL, node_info::Y}, {jNL, node_info::X}, elL, elNL);
                                  counter_nonloc({iL, node_info::Y}, {jNL, node_info::Y}, elL, elNL);
                              });

        shifts_nonloc[0] = shifts_loc.back();
        shifts_bound_nonloc[0] = shifts_bound_loc.back();
        for(size_t i = 1; i < shifts_nonloc.size(); ++i)
        {
            shifts_nonloc[i] += shifts_nonloc[i-1];
            shifts_bound_nonloc[i] += shifts_bound_nonloc[i-1];
        }
    }

    return {std::move(shifts_loc), std::move(shifts_bound_loc), std::move(shifts_nonloc), std::move(shifts_bound_nonloc)};
}

static std::array<std::vector<Eigen::Triplet<double, node_info>>, 2>
    triplets_fill(const mesh_2d<double> &mesh, parameters<double> params,
                  const std::vector<std::tuple<boundary_type, std::function<double(double, double)>,
                                               boundary_type, std::function<double(double, double)>>> &bounds_cond,
                  const double p1, const std::function<double(double, double, double, double)> &influence_fun)
{
    static constexpr double MAX_LOCAL_WEIGHT = 0.999;
    bool nonlocal = p1 < MAX_LOCAL_WEIGHT;

    const std::set<node_info> kinematic_nodes = kinematic_nodes_set(mesh, bounds_cond);
    auto [shifts_loc, shifts_bound_loc, shifts_nonloc, shifts_bound_nonloc] = mesh_analysis(mesh, kinematic_nodes, nonlocal);

    std::vector<Eigen::Triplet<double, node_info>> triplets      (nonlocal ? shifts_nonloc.back()       : shifts_loc.back()),
                                                   triplets_bound(nonlocal ? shifts_bound_nonloc.back() : shifts_bound_loc.back());
    std::cout << "Triplets count: " << triplets.size() + triplets_bound.size() << std::endl;
    for(auto [it, i] = std::make_tuple(kinematic_nodes.cbegin(), size_t(0)); it != kinematic_nodes.cend(); ++it, ++i)
    {
        uint32_t index = 2 * it->number + it->comp;
        triplets[i] = Eigen::Triplet<double, node_info>({index, it->comp}, {index, it->comp}, 1.);
    }

    const std::vector<uint32_t> shifts_quad = quadrature_shifts_init(mesh);
    const matrix<double> all_jacobi_matrices = approx_all_jacobi_matrices(mesh, shifts_quad);
    const std::array<double, 3> coeffs = {            params.E / (1. - params.nu*params.nu),
                                          params.nu * params.E / (1. - params.nu*params.nu),
                                                0.5 * params.E / (1. + params.nu)           };

    const std::function<void(node_info, node_info, size_t)>
        filler_loc =
            [&mesh, &kinematic_nodes, &shifts_loc, &shifts_bound_loc, &triplets, &triplets_bound, &shifts_quad, &all_jacobi_matrices, p1, &coeffs]
            (node_info node_i, node_info node_j, size_t el)
            {
                node_info glob_i = {mesh.node_number(el, node_i.number), node_i.comp},
                          glob_j = {mesh.node_number(el, node_j.number), node_j.comp},
                          row = 2 * glob_i.number + glob_i.comp,
                          col = 2 * glob_j.number + glob_j.comp;
                if(row >= col)
                {
                    double integral = p1 * integrate_loc(mesh.element_2d(mesh.element_type(el)), node_i, node_j, all_jacobi_matrices, shifts_quad[el], coeffs);
                    if(kinematic_nodes.find(glob_i) == kinematic_nodes.cend() &&
                       kinematic_nodes.find(glob_j) == kinematic_nodes.cend())
                        triplets[shifts_loc[el]++] = Eigen::Triplet<double, node_info>(row, col, integral);
                    else if(glob_i != glob_j)
                        triplets_bound[shifts_bound_loc[el]++] = kinematic_nodes.find(glob_j) == kinematic_nodes.cend() ?
                                                                 Eigen::Triplet<double, node_info>(col, row, integral) :
                                                                 Eigen::Triplet<double, node_info>(row, col, integral);
                }
            };

    mesh_run_loc(mesh, [&filler_loc](size_t i, size_t j, size_t el)
                       {
                           filler_loc({i, node_info::X}, {j, node_info::X}, el);
                           filler_loc({i, node_info::X}, {j, node_info::Y}, el);
                           filler_loc({i, node_info::Y}, {j, node_info::X}, el);
                           filler_loc({i, node_info::Y}, {j, node_info::Y}, el);
                       });

    if(nonlocal)
    {
        const matrix<double> all_quad_coords = approx_all_quad_nodes_coords(mesh, shifts_quad);

        const std::function<void(node_info, node_info, size_t, size_t)>
        filler_nonloc =
            [&mesh, &kinematic_nodes, &triplets, &triplets_bound, &shifts_nonloc, &shifts_bound_nonloc,
             &shifts_quad, &all_jacobi_matrices, &all_quad_coords, &influence_fun, p2 = 1. - p1, &coeffs]
            (node_info node_iL, node_info node_jNL, size_t elL, size_t elNL)
            {
                node_info glob_iL  = {mesh.node_number(elL,  node_iL.number),  node_iL.comp},
                          glob_jNL = {mesh.node_number(elNL, node_jNL.number), node_jNL.comp},
                          row = 2 * glob_iL.number  + glob_iL.comp,
                          col = 2 * glob_jNL.number + glob_jNL.comp;
                if(row >= col)
                {
                    double integral = p2 * integrate_nonloc(mesh.element_2d(mesh.element_type(elL )),
                                                            mesh.element_2d(mesh.element_type(elNL)), 
                                                            node_iL, node_jNL, shifts_quad[elL], shifts_quad[elNL],
                                                            all_quad_coords, all_jacobi_matrices, influence_fun, coeffs);
                    if(kinematic_nodes.find(glob_iL)  == kinematic_nodes.cend() &&
                       kinematic_nodes.find(glob_jNL) == kinematic_nodes.cend())
                        triplets[shifts_nonloc[elL]++] = Eigen::Triplet<double, node_info>(row, col, integral);
                    else if(glob_iL != glob_jNL)
                        triplets_bound[shifts_bound_nonloc[elL]++] = kinematic_nodes.find(glob_jNL) == kinematic_nodes.cend() ?
                                                                     Eigen::Triplet<double, node_info>(col, row,  integral) :
                                                                     Eigen::Triplet<double, node_info>(row, col, integral);
                }
            };

        mesh_run_nonloc(mesh, [&filler_nonloc](size_t iL, size_t jNL, size_t elL, size_t elNL)
                              {
                                  filler_nonloc({iL, node_info::X}, {jNL, node_info::X}, elL, elNL);
                                  filler_nonloc({iL, node_info::X}, {jNL, node_info::Y}, elL, elNL);
                                  filler_nonloc({iL, node_info::Y}, {jNL, node_info::X}, elL, elNL);
                                  filler_nonloc({iL, node_info::Y}, {jNL, node_info::Y}, elL, elNL);
                              });
    }

    return {std::move(triplets), std::move(triplets_bound)};
}

static void create_matrix(const mesh_2d<double> &mesh, parameters<double> params,
                          const std::vector<std::tuple<boundary_type, std::function<double(double, double)>,
                                                       boundary_type, std::function<double(double, double)>>> &bounds_cond,
                          Eigen::SparseMatrix<double> &K, Eigen::SparseMatrix<double> &K_bound,
                          const double p1, const std::function<double(double, double, double, double)> &influence_fun)
{
    double time = omp_get_wtime();
    auto [triplets, triplets_bound] = triplets_fill(mesh, params, bounds_cond, p1, influence_fun);
    std::cout << "Triplets calc: " << omp_get_wtime() - time << std::endl;

    K_bound.setFromTriplets(triplets_bound.cbegin(), triplets_bound.cend());
    triplets_bound.clear();
    K.setFromTriplets(triplets.cbegin(), triplets.cend());
    std::cout << "Nonzero elemets count: " << K.nonZeros() + K_bound.nonZeros() << std::endl;
}

void stationary(const std::string &path, const mesh_2d<double> &mesh, parameters<double> params,
                const std::vector<std::tuple<boundary_type, std::function<double(double, double)>,
                                             boundary_type, std::function<double(double, double)>>> &bounds_cond,
                const double p1, const std::function<double(double, double, double, double)> &influence_fun)
{
    Eigen::VectorXd f = Eigen::VectorXd::Zero(2*mesh.nodes_count());;
    Eigen::SparseMatrix<double> K(2*mesh.nodes_count(), 2*mesh.nodes_count()),
                                K_bound(2*mesh.nodes_count(), 2*mesh.nodes_count());
    
    create_matrix(mesh, params, bounds_cond, K, K_bound, p1, influence_fun);

    boundary_condition(mesh, kinematic_nodes_vectors(mesh, bounds_cond), bounds_cond, 1., K_bound, f);

    Eigen::ConjugateGradient<Eigen::SparseMatrix<double>, Eigen::Lower> solver;
    solver.compute(K);
    Eigen::VectorXd u = solver.solve(f);

    Eigen::VectorXd eps11 = Eigen::VectorXd::Zero(mesh.nodes_count()),
                    eps12 = Eigen::VectorXd::Zero(mesh.nodes_count()),
                    eps22 = Eigen::VectorXd::Zero(mesh.nodes_count());

    //save_as_vtk(path, mesh, u);

    std::ofstream fout_x(std::string("results//text_x_nonloc.csv")),
                  fout_y(std::string("results//text_y_nonloc.csv"));
    fout_x.precision(20);
    fout_y.precision(20);
    for(size_t i = 0; i < mesh.nodes_count(); ++i)
    {
        fout_x << mesh.coord(i, 0) << "," << mesh.coord(i, 1) << "," << u(2*i) << std::endl;
        fout_y << mesh.coord(i, 0) << "," << mesh.coord(i, 1) << "," << u(2*i+1) << std::endl;
    }
}

}