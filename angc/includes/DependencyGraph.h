#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <queue>
#include <algorithm>

namespace angara {

/// Builds a directed acyclic graph of module dependencies and provides
/// topological ordering for parallel compilation and transitive-dependency
/// queries for incremental rebuilds.
///
/// Nodes are identified by their absolute source-file path.
/// Edges point from importer to importee: A → B means "A imports B".
class DependencyGraph {
public:
    /// Registers a module in the graph.  Safe to call more than once for the
    /// same path (subsequent calls are no-ops).
    void addModule(const std::string& path,
                   const std::string& /*name*/,
                   const std::vector<std::string>& import_paths) {
        // Ensure the node exists (even if no edges).
        if (m_nodes.find(path) == m_nodes.end()) {
            m_nodes[path] = {};
            m_reverse[path] = {};
        }

        for (const auto& dep : import_paths) {
            // Skip self-imports (shouldn't happen, but be defensive).
            if (dep == path) continue;

            // Add forward edge: path → dep
            m_nodes[path].insert(dep);
            // Add reverse edge: dep ← path
            m_reverse[dep].insert(path);

            // Ensure the dep node exists.
            if (m_nodes.find(dep) == m_nodes.end()) {
                m_nodes[dep] = {};
                m_reverse[dep] = {};
            }
        }
    }

    /// Returns the set of all registered module paths.
    std::set<std::string> allModules() const {
        std::set<std::string> result;
        for (const auto& [path, _] : m_nodes) {
            result.insert(path);
        }
        return result;
    }

    /// Returns the set of direct importees for a module (who it depends on).
    const std::set<std::string>& dependencies(const std::string& path) const {
        static const std::set<std::string> empty;
        auto it = m_nodes.find(path);
        return it != m_nodes.end() ? it->second : empty;
    }

    /// Returns the set of direct importers of a module (who depends on it).
    const std::set<std::string>& dependents(const std::string& path) const {
        static const std::set<std::string> empty;
        auto it = m_reverse.find(path);
        return it != m_reverse.end() ? it->second : empty;
    }

    /// Returns all modules that transitively depend on `path` (the full
    /// forward-reachable set in the reverse-edge graph).  Includes `path`
    /// itself.  Used for incremental rebuilds: when `path` changes, every
    /// module in this set must be recompiled.
    std::set<std::string> transitiveDependents(const std::string& path) const {
        std::set<std::string> visited;
        std::queue<std::string> queue;
        queue.push(path);
        while (!queue.empty()) {
            auto current = queue.front(); queue.pop();
            if (!visited.insert(current).second) continue;
            auto it = m_reverse.find(current);
            if (it == m_reverse.end()) continue;
            for (const auto& importer : it->second) {
                if (visited.find(importer) == visited.end()) {
                    queue.push(importer);
                }
            }
        }
        return visited;
    }

    /// Topological sort using Kahn's algorithm.  Returns a vector of levels,
    /// where each level is a set of modules that can be compiled in parallel
    /// (none depend on each other).  Callers process levels sequentially and
    /// modules within a level concurrently.
    ///
    /// Returns an empty vector if a cycle is detected.
    std::vector<std::vector<std::string>> topologicalLevels() const {
        // Compute in-degree for each node.
        std::map<std::string, int> in_degree;
        for (const auto& [node, _] : m_nodes) {
            in_degree[node] = 0;
        }
        for (const auto& [node, deps] : m_nodes) {
            for (const auto& dep : deps) {
                in_degree[node]++;  // node depends on dep → node has indegree from dep
            }
        }

        // Actually, let me reconsider. The edge is "A imports B" = "A depends on B".
        // For topological ordering, A must come after B (B must compile first).
        // So the edge B → A means "B must be compiled before A".
        // In-degree counts how many things A depends on (that haven't been compiled yet).
        // Let me recompute properly:

        // Clear and rebuild in-degree correctly:
        // Edge: A imports B → A depends on B → B must be compiled first.
        // In-degree of A = number of modules A imports (A's dependencies).
        in_degree.clear();
        for (const auto& [node, _] : m_nodes) {
            in_degree[node] = 0;
        }
        for (const auto& [node, deps] : m_nodes) {
            in_degree[node] = static_cast<int>(deps.size());
        }

        std::queue<std::string> queue;
        for (const auto& [node, degree] : in_degree) {
            if (degree == 0) queue.push(node);
        }

        std::vector<std::vector<std::string>> levels;
        size_t processed = 0;

        while (!queue.empty()) {
            std::vector<std::string> current_level;
            size_t level_size = queue.size();
            for (size_t i = 0; i < level_size; ++i) {
                auto node = queue.front(); queue.pop();
                current_level.push_back(node);
                processed++;

                // "Release" all modules that import this node (dependents).
                auto it = m_reverse.find(node);
                if (it == m_reverse.end()) continue;
                for (const auto& dependent : it->second) {
                    if (--in_degree[dependent] == 0) {
                        queue.push(dependent);
                    }
                }
            }
            levels.push_back(std::move(current_level));
        }

        // If we didn't process all nodes, there's a cycle.
        if (processed != m_nodes.size()) {
            return {};  // cycle detected
        }

        return levels;
    }

    /// Returns true if the graph contains a cycle.
    bool hasCycle() const {
        return topologicalLevels().empty() && !m_nodes.empty();
    }

    /// Returns the number of modules in the graph.
    size_t size() const { return m_nodes.size(); }

private:
    // Forward edges: module_path → set of modules it imports (its dependencies).
    std::map<std::string, std::set<std::string>> m_nodes;
    // Reverse edges: module_path → set of modules that import it (its dependents).
    std::map<std::string, std::set<std::string>> m_reverse;
};

} // namespace angara
