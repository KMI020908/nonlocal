#include <iostream>
#include "finite_element_solver_base_1d.hpp"

namespace {

enum class element_type : uint8_t {
    LINEAR = 1,
    QUADRATIC = 2,
    QUBIC = 3
};

template<class T>
using finite_element_1d_ptr = std::unique_ptr<metamath::finite_element::element_1d_integrate_base<T>>;

template<class T>
static finite_element_1d_ptr<T> make_element(const element_type type) {
    switch(type) {
        case element_type::LINEAR: {
            using quadrature = metamath::finite_element::quadrature_1d<T, metamath::finite_element::gauss1>;
            using element_1d = metamath::finite_element::element_1d_integrate<T, metamath::finite_element::linear>;
            return std::make_unique<element_1d>(quadrature{});
        }

        case element_type::QUADRATIC: {
            using quadrature = metamath::finite_element::quadrature_1d<T, metamath::finite_element::gauss2>;
            using element_1d = metamath::finite_element::element_1d_integrate<T, metamath::finite_element::quadratic>;
            return std::make_unique<element_1d>(quadrature{});
        }

        case element_type::QUBIC: {
            using quadrature = metamath::finite_element::quadrature_1d<T, metamath::finite_element::gauss3>;
            using element_1d = metamath::finite_element::element_1d_integrate<T, metamath::finite_element::qubic>;
            return std::make_unique<element_1d>(quadrature{});
        }

        default:
            throw std::logic_error{"Unknown element type " + std::to_string(int(type))};
    }
}

}

int main(int argc, char** argv) {
    if (argc < 5) {
        std::cerr << "run format: program_name <element_type> <elements_count> <section>" << std::endl;
        return EXIT_FAILURE;
    }

    try {
        auto mesh = std::make_shared<mesh::mesh_1d<double, int>>(
            make_element<double>(element_type(std::stoi(argv[1]))),
            std::stoull(argv[2]), std::array{std::stod(argv[3]), std::stod(argv[4])}
        );

        std::cout << "section: [" << mesh->section().front() << ',' << mesh->section().back() << ']' << std::endl;
        std::cout << "elements count: " << mesh->elements_count() << std::endl;
        std::cout << "nodes count: " << mesh->nodes_count() << std::endl;
        std::cout << "element info: nodes count - " << mesh->element()->nodes_count() <<
                     "; qnodes count - " << mesh->element()->qnodes_count() << std::endl;

        for(size_t e = 0; e < mesh->elements_count(); ++e)
            std::cout << "element " << e << " begins node " << mesh->node_begin(e) << std::endl;

        std::cout << std::endl;

        for(size_t node = 0; node < mesh->nodes_count(); ++node) {
            const auto elements = mesh->node_elements(node);
            std::cout << "node " << node << " elements "
                      << elements.arr[0][0] << ' '
                      << elements.arr[0][1] << ' '
                      << elements.arr[1][0] << ' '
                      << elements.arr[1][1] << ' ' << std::endl;
        }


        nonlocal::finite_element_solver_base_1d<double, int> solver{mesh};

        nonlocal::equation_parameters<double> parameters;
        parameters.p1 = 1;
        parameters.r = 0;
        auto solution = solver.stationary(
            parameters,
            {
                std::pair{nonlocal::boundary_condition_t::FIRST_KIND, 0},
                std::pair{nonlocal::boundary_condition_t::FIRST_KIND, 1},
            },
            [](const double x) { return 0; },
            [](const double x, const double xp) { return 1; }
        );

        for(const double x : solution)
            std::cout << x << ' ';
        std::cout << std::endl;
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Unknown error" << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}