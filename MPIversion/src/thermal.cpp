#include <petsc.h>
#include "influence_functions.hpp"
#include "heat_equation_solver.hpp"

namespace {

void save_raw_data(const std::shared_ptr<mesh::mesh_2d<double>>& mesh,
                   const std::vector<double>& T,
                   const std::array<std::vector<double>, 2>& gradient) {
    std::ofstream Tout{"T.csv"};
    std::ofstream Tx{"Tx.csv"};
    std::ofstream Ty{"Ty.csv"};
    for(size_t i = 0; i < mesh->nodes_count(); ++i) {
        Tout << mesh->node(i)[0] << ',' << mesh->node(i)[1] << ',' << T[i] << '\n';
        Tx   << mesh->node(i)[0] << ',' << mesh->node(i)[1] << ',' << gradient[0][i] << '\n';
        Ty   << mesh->node(i)[0] << ',' << mesh->node(i)[1] << ',' << gradient[1][i] << '\n';
    }
}

}

int main(int argc, char** argv) {
    PetscErrorCode ierr = PetscInitialize(&argc, &argv, nullptr, nullptr); CHKERRQ(ierr);

    if(argc < 2) {
        std::cerr << "Input format [program name] <path to mesh>";
        return EXIT_FAILURE;
    }

    try {
        std::cout.precision(7);

        static constexpr double r = 0.1, p1 = 1;
        static const nonlocal::influence::polynomial<double, 2, 1> bell(r);

        auto mesh = std::make_shared<mesh::mesh_2d<double>>(argv[1]);
        auto mesh_info = std::make_shared<mesh::mesh_info<double, int>>(mesh);
        mesh_info->find_neighbours(r, mesh::balancing_t::SPEED);

        nonlocal::heat::heat_equation_solver<double, int> fem_sol{mesh_info};

        const auto T = fem_sol.stationary(
            { // Граничные условия
                {   // Down
                    nonlocal::heat::boundary_t::TEMPERATURE,
                    [](const std::array<double, 2>& x) { return x[0]*x[0] + x[1]*x[1]; },
                },

                {   // Right
                    nonlocal::heat::boundary_t::TEMPERATURE,
                    [](const std::array<double, 2>& x) { return x[0]*x[0] + x[1]*x[1]; },
                },

                {   // Up
                    nonlocal::heat::boundary_t::TEMPERATURE,
                    [](const std::array<double, 2>& x) { return x[0]*x[0] + x[1]*x[1]; },
                },

                {   // Left
                    nonlocal::heat::boundary_t::TEMPERATURE,
                    [](const std::array<double, 2>& x) { return x[0]*x[0] + x[1]*x[1]; },
                }
            },
            [](const std::array<double, 2>&) { return -4; }, // Правая часть
            p1, // Вес
            bell // Функция влияния
        );

        int rank = -1;
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        if (rank == 0) {
            //fem_sol.save_as_vtk("heat.vtk", T);
            save_raw_data(mesh, T, mesh_info->calc_gradient(T));
            std::cout << mesh_info->integrate_solution(T) << std::endl;
            //std::cout << fem_sol.integrate_solution(T) << std::endl;
        }
    } catch(const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    } catch(...) {
        std::cerr << "Unknown error." << std::endl;
        return EXIT_FAILURE;
    }

    return PetscFinalize();
}