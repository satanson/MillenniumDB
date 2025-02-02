#include "property_path_a_star_iter_enum.h"

#include <cassert>

#include "base/ids/var_id.h"
#include "relational_model/execution/binding_id_iter/property_paths/path_manager.h"
#include "storage/index/bplus_tree/bplus_tree.h"
#include "storage/index/bplus_tree/bplus_tree_leaf.h"
#include "storage/index/record.h"

using namespace std;
using namespace AStarIterEnum;

PropertyPathAStarIterEnum::PropertyPathAStarIterEnum(ThreadInfo*   _thread_info,
                                                     BPlusTree<1>& _nodes,
                                                     BPlusTree<4>& _type_from_to_edge,
                                                     BPlusTree<4>& _to_type_from_edge,
                                                     VarId         _path_var,
                                                     Id            _start,
                                                     VarId         _end,
                                                     PathAutomaton _automaton) :
    thread_info       (_thread_info),
    nodes             (_nodes),
    type_from_to_edge (_type_from_to_edge),
    to_type_from_edge (_to_type_from_edge),
    path_var          (_path_var),
    start             (_start),
    end               (_end),
    automaton         (_automaton)
    { }


void PropertyPathAStarIterEnum::begin(BindingId& _parent_binding) {
    parent_binding = &_parent_binding;
    is_first = true;
    // Create and add start object id
    ObjectId start_object_id(std::holds_alternative<ObjectId>(start) ?
        std::get<ObjectId>(start) :
        (*parent_binding)[std::get<VarId>(start)]);

    open.emplace(automaton.get_start(),
                    start_object_id,
                    automaton.distance_to_final[automaton.get_start()]);

    visited.emplace(automaton.get_start(),
                    start_object_id,
                    nullptr,
                    true,
                    ObjectId::get_null());

    min_ids[2] = 0;
    max_ids[2] = 0xFFFFFFFFFFFFFFFF;
    min_ids[3] = 0;
    max_ids[3] = 0xFFFFFFFFFFFFFFFF;
    // pos 0 and 1 will be set at next()
}


bool PropertyPathAStarIterEnum::next() {
    if (is_first) {
        is_first = false;
        auto& current_state = open.top();
        auto start_node_iter = nodes.get_range(&thread_info->interruption_requested,
                                               Record<1>({current_state.object_id.id}),
                                               Record<1>({current_state.object_id.id}));
        // Return false if node does not exists in bd
        if (start_node_iter->next() == nullptr) {
            return false;
        }
        if (automaton.start_is_final) {
            auto current_key = SearchState(automaton.get_final_state(),
                                           current_state.object_id,
                                           nullptr,
                                           true,
                                           ObjectId::get_null());
            // set binding;
            auto path_id = path_manager.set_path(visited.insert(current_key).first.operator->(), path_var);
            parent_binding->add(path_var, path_id);
            parent_binding->add(end, current_state.object_id);
            results_found++;
            return true;
        }
    }
    while (open.size() > 0) {
        auto reached_state = current_state_has_next();
        if (reached_state != visited.end()) {
            auto& current_state = open.top();
            open.emplace(reached_state->state, reached_state->object_id, current_state.distance);
            if (reached_state->state == automaton.get_final_state()) {
                // set binding;
                auto path_id = path_manager.set_path(reached_state.operator->(), path_var);
                parent_binding->add(path_var, path_id);
                parent_binding->add(end, reached_state->object_id);
                results_found++;
                return true;
            }
        } else {
            open.pop();
        }
    }
    return false;
}


unordered_set<SearchState, SearchStateHasher>::iterator
PropertyPathAStarIterEnum::current_state_has_next()
{
    auto current_state = &open.top();
    if (current_state->iter == nullptr) {
        if (current_state->transition < automaton.transitions[current_state->state].size()) {
            set_iter();
            current_state = &open.top(); // set_iter modifies open.top()
        } else {
            return visited.end();
        }
    }

    while (current_state->transition < automaton.transitions[current_state->state].size()) {
        auto& transition = automaton.transitions[current_state->state][current_state->transition];
        auto child_record = current_state->iter->next();
        // Iterate over next_childs
        while (child_record != nullptr) {
            auto current_key = SearchState(current_state->state,
                                           current_state->object_id,
                                           nullptr,
                                           true,
                                           ObjectId::get_null());

            auto next_state_key = SearchState(transition.to,
                                              ObjectId(child_record->ids[2]),
                                              visited.find(current_key).operator->(),
                                              transition.inverse,
                                              transition.label);

            // Check child is not already visited
            auto inserted_state = visited.insert(next_state_key);
            // inserted_state.second is true if state was inserted
            if (inserted_state.second) {
                return inserted_state.first;
            }
            child_record = current_state->iter->next();
        }
        // Constructs new iter
        if (current_state->transition < automaton.transitions[current_state->state].size()) {
            set_iter();
            current_state = &open.top(); // set_iter modified open.top()
        }
    }
    return visited.end();
}


void PropertyPathAStarIterEnum::set_iter() {
    // Get pointer from priority queue
    auto current_state = &open.top();
    // Create a copy of current state
    PriorityIterState new_state(current_state->state, current_state->object_id, current_state->distance);
    // Sets the transition index that will be read
    if (current_state->iter != nullptr) {
        new_state.transition = current_state->transition + 1;
    } else {
        new_state.transition = 0;
    }
    // Get transition
    const auto& transition = automaton.transitions[new_state.state][new_state.transition];
    // Construct iter
    if (transition.inverse) {
        min_ids[0] = new_state.object_id.id;
        max_ids[0] = new_state.object_id.id;
        min_ids[1] = transition.label.id;
        max_ids[1] = transition.label.id;
        new_state.iter = to_type_from_edge.get_range(&thread_info->interruption_requested,
                                                     Record<4>(min_ids),
                                                     Record<4>(max_ids));
    } else {
        min_ids[0] = transition.label.id;
        max_ids[0] = transition.label.id;
        min_ids[1] = new_state.object_id.id;
        max_ids[1] = new_state.object_id.id;
        new_state.iter = type_from_to_edge.get_range(&thread_info->interruption_requested,
                                                     Record<4>(min_ids),
                                                     Record<4>(max_ids));
    }
    bpt_searches++;
    // Remove current_state of priority queue
    open.pop();
    // Insert new_state. It wil be inserted first in the priority queue,
    // because has the same priority of current_state (That was first)
    open.push(move(new_state));
}


void PropertyPathAStarIterEnum::reset() {
    // Empty open and visited
    priority_queue<PriorityIterState> empty;
    open.swap(empty);
    visited.clear();
    is_first = true;

    // Add start object id to open and visited
    ObjectId start_object_id(std::holds_alternative<ObjectId>(start) ?
        std::get<ObjectId>(start) :
        (*parent_binding)[std::get<VarId>(start)]);

    open.emplace(automaton.get_start(),
                 start_object_id,
                 automaton.distance_to_final[automaton.get_start()]);

    visited.emplace(automaton.get_start(),
                    start_object_id,
                    nullptr,
                    true,
                    ObjectId::get_null());
}


void PropertyPathAStarIterEnum::assign_nulls() {
    parent_binding->add(end, ObjectId::get_null());
}


void PropertyPathAStarIterEnum::analyze(std::ostream& os, int indent) const {
    os << std::string(indent, ' ');
    os << "PropertyPathAStarIterEnum(bpt_searches: " << bpt_searches
       << ", found: " << results_found <<")\n";
}
