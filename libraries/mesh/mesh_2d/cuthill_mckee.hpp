#ifndef NONLOCAL_CUTHILL_MCKEE_HPP
#define NONLOCAL_CUTHILL_MCKEE_HPP

#include "mesh_proxy.hpp"

#include "nonlocal_constants.hpp"

#include <map>
#include <set>

namespace nonlocal::mesh {

class _cuthill_mckee final {
    explicit _cuthill_mckee() noexcept = default;

    class initializer_base {
        std::vector<bool> _is_include;

    protected:
        explicit initializer_base(const size_t size)
            : _is_include(size, false) {}

        bool check_neighbour(const size_t node, const size_t neighbour) {
            const bool result = node != neighbour && !_is_include[neighbour];
            if (result)
                _is_include[neighbour] = true;
            return result;
        }

    public:
        void reset_include() {
            std::fill(_is_include.begin(), _is_include.end(), false);
        }
    };

    template<class I>
    class shifts_initializer final : public initializer_base {
        std::vector<I>& _shifts;

    public:
        explicit shifts_initializer(std::vector<I>& shifts)
            : initializer_base{shifts.size() - 1}
            , _shifts{shifts} {}

        void add(const size_t node, const size_t neighbour) {
            if (check_neighbour(node, neighbour))
                ++_shifts[node + 1];
        }
    };

    template<class I>
    class indices_initializer final : public initializer_base {
        const std::vector<I>& _shifts;
        std::vector<I>& _indices;
        I _node_shift = 0;

    public:
        explicit indices_initializer(std::vector<I>& indices,
                                     const std::vector<I>& shifts)
            : initializer_base{shifts.size() - 1}
            , _shifts{shifts}
            , _indices{indices} {}

        void reset_include() {
            _node_shift = 0;
            initializer_base::reset_include();
        }

        void add(const size_t node, const size_t neighbour) {
            if (check_neighbour(node, neighbour)) {
                _indices[_shifts[node] + _node_shift] = neighbour;
                ++_node_shift;
            }
        }
    };

    template<class I>
    struct node_graph final {
        std::vector<I> shifts, indices;

        size_t neighbours_count(const size_t node) const {
            return shifts[node + 1] - shifts[node];
        }
    };

    template<class I>
    static void prepare_shifts(std::vector<I>& shifts) {
        for(size_t i = 1; i < shifts.size(); ++i)
            shifts[i] += shifts[i - 1];
    }

    template<theory_t Theory, class T, class I, class Initializer>
    static void init_vector(const mesh_proxy<T, I>& mesh, Initializer&& initializer) {
#pragma omp parallel for default(none) shared(mesh) firstprivate(initializer) schedule(dynamic)
        for(size_t node = mesh.first_node(); node < mesh.last_node(); ++node) {
            initializer.reset_include();
            for(const I eL : mesh.nodes_elements_map(node)) {
                if constexpr (Theory == theory_t::LOCAL)
                    for(size_t jL = 0; jL < mesh.mesh().nodes_count(eL); ++jL)
                        initializer.add(node, mesh.mesh().node_number(eL, jL));
                if constexpr (Theory == theory_t::NONLOCAL)
                    for(const I eNL : mesh.neighbors(eL))
                        for(size_t jNL = 0; jNL < mesh.mesh().nodes_count(eNL); ++jNL)
                            initializer.add(node, mesh.mesh().node_number(eNL, jNL));
            }
        }
    }

    template<class T, class I>
    static node_graph<I> init_graph(const mesh_proxy<T, I>& mesh, const bool is_nonlocal) {
        _cuthill_mckee::node_graph<I> graph;
        graph.shifts.resize(mesh.mesh().nodes_count() + 1, 0);
        if (is_nonlocal) init_vector<theory_t::NONLOCAL>(mesh, shifts_initializer<I>{graph.shifts});
        else             init_vector<theory_t::   LOCAL>(mesh, shifts_initializer<I>{graph.shifts});
        prepare_shifts(graph.shifts);
        graph.indices.resize(graph.shifts.back());
        if (is_nonlocal) init_vector<theory_t::NONLOCAL>(mesh, indices_initializer<I>{graph.indices, graph.shifts});
        else             init_vector<theory_t::   LOCAL>(mesh, indices_initializer<I>{graph.indices, graph.shifts});
        return graph;
    }

    template<class I>
    static I node_with_minimum_neighbours(const node_graph<I>& graph) {
        I curr_node = 0, min_neighbours_count = std::numeric_limits<I>::max();
        for(size_t node = 0; node < graph.shifts.size() - 1; ++node)
            if (const I neighbours_count = graph.neighbours_count(node); neighbours_count < min_neighbours_count) {
                curr_node = node;
                min_neighbours_count = neighbours_count;
            }
        return curr_node;
    }

    template<class I>
    static std::vector<I> calculate_permutation(const node_graph<I>& graph, const I init_node) {
        std::vector<I> permutation(graph.shifts.size() - 1, I{-1});
        I curr_index = 0;
        permutation[init_node] = curr_index++;
        std::unordered_set<I> curr_layer{init_node}, next_layer;
        while (curr_index < permutation.size()) {
            next_layer.clear();
            for(const I node : curr_layer) {
                std::multimap<I, I> neighbours;
                for(I shift = graph.shifts[node]; shift < graph.shifts[node+1]; ++shift)
                    if (const I neighbour_node = graph.indices[shift]; permutation[neighbour_node] == -1)
                        neighbours.emplace(graph.neighbours_count(neighbour_node), neighbour_node);
                for(const auto [_, neighbour] : neighbours) {
                    next_layer.emplace(neighbour);
                    permutation[neighbour] = curr_index++;
                }
            }
            std::swap(curr_layer, next_layer);
        }
        return permutation;
    }

public:
    template<class T, class I>
    friend std::vector<I> cuthill_mckee(const mesh_proxy<T, I>& mesh, const bool is_nonlocal);
};

template<class T, class I>
std::vector<I> cuthill_mckee(const mesh_proxy<T, I>& mesh, const bool is_nonlocal) {
    const _cuthill_mckee::node_graph<I> graph = _cuthill_mckee::init_graph(mesh, is_nonlocal);
    return _cuthill_mckee::calculate_permutation(graph, _cuthill_mckee::node_with_minimum_neighbours(graph));
}

}

#endif