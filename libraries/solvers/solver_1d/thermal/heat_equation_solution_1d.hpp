#ifndef NONLOCAL_HEAT_EQUATION_SOLUTION_1D_HPP
#define NONLOCAL_HEAT_EQUATION_SOLUTION_1D_HPP

#include "solution_1d.hpp"
#include "mesh_1d_utils.hpp"

namespace nonlocal::thermal {

template<class T>
class heat_equation_solution_1d : public solution_1d<T> {
    using _base = solution_1d<T>;

    const std::vector<T> _temperature;
    const std::vector<parameter_1d_sptr<T>> _parameters;
    std::optional<std::vector<T>> _flux;
    
public:
    using _base::mesh;

    template<class Parameter, class Vector>
    explicit heat_equation_solution_1d(const std::shared_ptr<mesh::mesh_1d<T>>& mesh,
                                       const std::vector<Parameter>& parameters,
                                       const Vector& solution);
    ~heat_equation_solution_1d() noexcept override = default;

    const std::vector<T>& temperature() const noexcept;
    const std::vector<T>& flux() const;
    const parameter_1d_sptr<T>& parameter(const size_t segment) const noexcept;

    bool is_flux_calculated() const noexcept;
    const std::vector<T>& calc_flux();
};

template<class T>
template<class Parameter, class Vector>
heat_equation_solution_1d<T>::heat_equation_solution_1d(const std::shared_ptr<mesh::mesh_1d<T>>& mesh,
                                                        const std::vector<Parameter>& parameters,
                                                        const Vector& solution)
    : _base{mesh, get_models(parameters)}
    , _temperature(solution.cbegin(), std::next(solution.cbegin(), mesh->nodes_count()))
    , _parameters{get_physical_parameters(parameters)} {}

template<class T>
const std::vector<T>& heat_equation_solution_1d<T>::temperature() const noexcept {
    return _temperature;
}

template<class T>
const std::vector<T>& heat_equation_solution_1d<T>::flux() const {
    return *_flux;
}

template<class T>
const parameter_1d_sptr<T>& heat_equation_solution_1d<T>::parameter(const size_t segment) const noexcept {
    return _parameters[segment];
}

template<class T>
bool heat_equation_solution_1d<T>::is_flux_calculated() const noexcept {
    return _flux;
}

template<class T>
const std::vector<T>& heat_equation_solution_1d<T>::calc_flux() {
    const auto& el = mesh().element();
    const auto quadratures = std::ranges::iota_view{0u, el.qnodes_count()};
    std::vector<T> gradient = mesh::utils::gradient_in_qnodes(mesh(), temperature());

    for(const size_t segment : mesh().segments()) {
        const auto segment_elements = mesh().elements(segment);
        const theory_t theory = theory_type(_base::model(segment).local_weight);
        std::vector<T> gradient_nonlocal(theory == theory_t::NONLOCAL ? mesh().elements_count(segment) * el.qnodes_count() : 0, T{0});

        if (parameter(segment)->type == coefficients_t::SPACE_DEPENDENT || parameter(segment)->type == coefficients_t::SOLUTION_DEPENDENT)
            throw std::domain_error{"Oops! Right now I can calc flux only if conductivity is constant!"};

        if (theory == theory_t::NONLOCAL) {
            for(const size_t eL : segment_elements) {
                const size_t qshiftL = (eL - segment_elements.front()) * el.qnodes_count();
                for(const size_t eNL : mesh().neighbours(eL)) {
                    const size_t qshiftNL = eNL * el.qnodes_count();
                    for(const size_t qL : quadratures)
                        for(const size_t qNL : quadratures) {
                            const T influence_weight = _base::model(segment).influence(mesh().qnode_coord(eL,  qL), 
                                                                                       mesh().qnode_coord(eNL, qNL));
                            gradient_nonlocal[qshiftL + qL] -= el.weight(qNL) * influence_weight * gradient[qshiftNL + qNL];
                        }
                }
            }
            using namespace metamath::functions;
            gradient_nonlocal *= nonlocal_weight(_base::model(segment).local_weight) * 
                                 parameter_cast<coefficients_t::CONSTANTS>(*parameter(segment)).conductivity * 
                                 mesh().jacobian(segment);
        }

        const size_t qshiftNL = el.qnodes_count() * segment_elements.front();
        for(const size_t qshiftL : std::ranges::iota_view{qshiftNL, el.qnodes_count() * *segment_elements.end()}) {
            gradient[qshiftL] *= -_base::model(segment).local_weight * parameter_cast<coefficients_t::CONSTANTS>(*parameter(segment)).conductivity;
            if (theory == theory_t::NONLOCAL)
                gradient[qshiftL] += gradient_nonlocal[qshiftL - qshiftNL];
        }
    }

    _flux = mesh::utils::from_qnodes_to_nodes(mesh(), gradient);
    return *_flux;
}

}

#endif