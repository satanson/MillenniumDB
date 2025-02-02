#ifndef BASE__OP_PATH_SEQUENCE_H_
#define BASE__OP_PATH_SEQUENCE_H_

#include <memory>
#include <vector>
#include <iostream>

#include "base/parser/logical_plan/op/op.h"
#include "base/parser/logical_plan/op/property_paths/path_automaton.h"

class OpPathSequence : public OpPath {
public:
    std::vector<std::unique_ptr<OpPath>> sequence;
    const bool is_nullable;

     static bool get_nullable(const std::vector<std::unique_ptr<OpPath>>& sequence) {
        for (const auto& seq : sequence) {
            if (!seq->nullable()) {
                return false;
            }
        }
        return true;
    }

    OpPathSequence(std::vector<std::unique_ptr<OpPath>> _sequence) :
        sequence    (std::move(_sequence)),
        is_nullable (get_nullable(sequence)) { }

    OpPathSequence(const OpPathSequence& other) :
        is_nullable (other.nullable())
    {
        for (const auto& seq : other.sequence) {
            sequence.push_back(seq->duplicate());
        }
    }

    void accept_visitor(OpVisitor& visitor) override {
        visitor.visit(*this);
    }

    std::string to_string() const override {
        std::string sequence_string = "(";
        sequence_string.append(sequence[0]->to_string());
        for (std::size_t i = 1; i < sequence.size(); i++) {
            sequence_string.append("/");
            sequence_string.append(sequence[i]->to_string());
        }
        sequence_string.append(")");
        return sequence_string;
    }

    std::ostream& print_to_ostream(std::ostream& os, int indent=0) const override{
        os << std::string(indent, ' ');
        os << "OpSequence()\n";

        for (auto& seq : sequence) {
            seq->print_to_ostream(os, indent + 2);
        }

        return os;
    };

    bool nullable() const {
        return is_nullable;
    }

    std::unique_ptr<OpPath> duplicate() const override {
        return std::make_unique<OpPathSequence>(*this);
    }

    OpPathType type() const { return OpPathType::OP_PATH_SEQUENCE; }

    PathAutomaton get_automaton() const override {
        auto sequence_automaton = sequence[0]->get_automaton();
        // For each sequence child create and automaton
        for (size_t i = 1; i < sequence.size(); i++) {
            auto child_automaton = sequence[i]->get_automaton();
            sequence_automaton.rename_and_merge(child_automaton);
            // Connect end state of sequence automaton to start of child
            for (const auto& end_state : sequence_automaton.end_states) {
                sequence_automaton.add_epsilon_transition(end_state, child_automaton.get_start());
            }
            // Replace sequence automaton's end states by child's end states
            sequence_automaton.end_states = std::move(child_automaton.end_states);
        }
        return sequence_automaton;
    }

    std::unique_ptr<OpPath> invert() const override {
        std::vector<std::unique_ptr<OpPath>> invert_sequence;
        for (size_t i = 0; i < sequence.size(); i++) {
            invert_sequence.push_back(sequence[sequence.size() - 1 - i]->invert());
        }
        return std::make_unique<OpPathSequence>(move(invert_sequence));
    }

};

#endif // BASE__OP_PATH_SEQUENCE_H_
