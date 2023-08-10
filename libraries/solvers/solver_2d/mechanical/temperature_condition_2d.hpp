#ifndef NONLOCAL_TEMPERATURE_CONDITION_2D_HPP
#define NONLOCAL_TEMPERATURE_CONDITION_2D_HPP

#include "mechanical_solution_2d.hpp"
#include "mesh_2d_utils.hpp"
#include <Eigen/Dense>

namespace nonlocal::mechanical {

template<class T, class I>
class _temperature_condition final {
    const std::vector<T> _temperature_in_qnodes;
    const mechanical_parameters_2d<T>& _parameters;
    const mesh::mesh_2d<T, I>& _mesh;

    static std::vector<T> approximate_delta_temperature_in_qnodes(const mesh::mesh_2d<T, I>& mesh,
                                                                  const mechanical_parameters_2d<T>& parameters) {
        std::vector<T> temperature_in_qnodes = nonlocal::mesh::utils::nodes_to_qnodes(mesh, parameters.delta_temperature);
        for(const std::string& group : mesh.container().groups_2d()) {
            const auto& parameter = parameters.materials.at(group).physical;
            const T factor = parameter.thermal_expansion * parameter.E(parameters.plane) / (T{1} - parameter.nu(parameters.plane));
            for(const size_t e : mesh.container().elements(group))
                for(const size_t qshift : mesh.quad_shifts_count(e))
                    temperature_in_qnodes[qshift] *= factor;
        }
        return temperature_in_qnodes;
    }

    explicit _temperature_condition(const mesh::mesh_2d<T, I>& mesh,
                                    const mechanical_parameters_2d<T>& parameters)
        : _temperature_in_qnodes{approximate_delta_temperature_in_qnodes(mesh, parameters)}
        , _parameters{parameters}
        , _mesh{mesh} {}

    std::array<T, 2> operator()(const size_t e, const size_t i) const {
        using namespace metamath::functions;
        std::array<T, 2> integral = {};
        size_t qshift = _mesh.quad_shift(e);
        const auto& el = _mesh.container().element_2d(e);
        for(const size_t q : el.qnodes())
            integral += el.weight(q) * _temperature_in_qnodes[qshift++] * _mesh.derivatives(e, i, q);
        return integral;
    }

    std::array<T, 2> operator()(const size_t eL, const size_t eNL, const size_t iL,
                                const std::function<T(const std::array<T, 2>&, const std::array<T, 2>&)>& influence) const {
        using namespace metamath::functions;
        std::array<T, 2> integral = {};
        const auto& elL = _mesh.container().element_2d(eL);
        const auto& elNL = _mesh.container().element_2d(eNL);
        for(const size_t qL : elL.qnodes()) {
            T inner_integral = T{0};
            size_t qshiftNL = _mesh.quad_shift(eNL);
            const std::array<T, 2>& qcoordL = _mesh.quad_coord(eL, qL);
            for(const size_t qNL : elNL.qnodes()) {
                const T weight = elNL.weight(qNL) * influence(qcoordL, _mesh.quad_coord(qshiftNL)) *
                                 mesh::jacobian(_mesh.jacobi_matrix(qshiftNL));
                inner_integral += weight * _temperature_in_qnodes[qshiftNL++];
            }
            integral += elL.weight(qL) * inner_integral * _mesh.derivatives(eL, iL, qL);
        }
        return integral;
    }

public:
    template<class U, class J>
    friend void temperature_condition(Eigen::Matrix<U, Eigen::Dynamic, 1>& f,
                                      const mesh::mesh_2d<U, J>& mesh,
                                      const mechanical_parameters_2d<U>& parameters);
};

template<class T, class I>
void temperature_condition(Eigen::Matrix<T, Eigen::Dynamic, 1>& f,
                           const mesh::mesh_2d<T, I>& mesh,
                           const mechanical_parameters_2d<T>& parameters) {
    using namespace metamath::functions;
    const _temperature_condition<T, I> integrator{mesh, parameters};
    const auto process_node = mesh.process_nodes();
#pragma omp parallel for default(none) shared(f, mesh, parameters, integrator, process_node) schedule(dynamic)
    for(size_t node = process_node.front(); node < *process_node.end(); ++node) {
        std::array<T, 2> integral = {};
        for(const I eL : mesh.elements(node)) {
            const size_t iL = mesh.global_to_local(eL, node);
            const auto& parameter = parameters.materials.at(mesh.container().group(eL));
            if (theory_type(parameter.model.local_weight) == theory_t::NONLOCAL) {
                const T nonlocal_weight = nonlocal::nonlocal_weight(parameter.model.local_weight);
                for(const I eNL : mesh.neighbours(eL))
                    integral += nonlocal_weight * integrator(eL, eNL, iL, parameter.model.influence);
            }
            integral += parameter.model.local_weight * integrator(eL, iL);
        }
        f[2 * node + X] += integral[X];
        f[2 * node + Y] += integral[Y];
    }
}

}

#endif