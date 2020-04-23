#include "Block.h"
#include "Function.h"

#include <sstream>

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Function.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

namespace dg {
namespace cd {

int Block::traversalCounter = 0;

const llvm::Instruction *Block::lastInstruction() const {
    if (!llvmInstructions_.empty()) {
        return llvmInstructions_.back();
    }
    return nullptr;
}

bool Block::addInstruction(const llvm::Instruction *instruction) {
    if (!instruction) {
        return false;
    }
    llvmInstructions_.push_back(instruction);
    return true;
}

bool Block::addCallee(const llvm::Function *llvmFunction, Function *function) {
    if (!llvmFunction || !function) {
        return false;
    }
    return callees_.emplace(llvmFunction, function).second;
}

bool Block::addFork(const llvm::Function *llvmFunction, Function *function) {
    if (!llvmFunction || !function) {
        return false;
    }
    return forks_.emplace(llvmFunction, function).second;
}

bool Block::addJoin(const llvm::Function *llvmFunction, Function *function) {
    if (!llvmFunction || !function) {
        return false;
    }
    return joins_.emplace(llvmFunction, function).second;
}

const std::map<const llvm::Function *, Function *> &Block::callees() const {
    return callees_;
}

std::map<const llvm::Function *, Function *> Block::callees() {
    return callees_;
}

const std::map<const llvm::Function *, Function *> &Block::forks() const {
    return forks_;
}

std::map<const llvm::Function *, Function *> Block::forks() {
    return forks_;
}

const std::map<const llvm::Function *, Function *> &Block::joins() const {
    return joins_;
}

std::map<const llvm::Function *, Function *> Block::joins() {
    return joins_;
}

bool Block::isCall() const {
    return !llvmInstructions_.empty() && llvmInstructions_.back()->getOpcode() == llvm::Instruction::Call;
}

bool Block::isArtificial() const {
    return llvmInstructions_.empty();
}

bool Block::isCallReturn() const {
    return isArtificial()           &&
           callReturn;
}

bool Block::isExit() const {
    return isArtificial() && !isCallReturn();
}

const llvm::BasicBlock *Block::llvmBlock() const {
    if (!llvmInstructions_.empty()) {
        return llvmInstructions_.back()->getParent();
    } else if (!predecessors().empty()) {
        for (auto predecessor : predecessors()) {
            if (predecessor->llvmInstructions_.size() > 0) {
                return predecessor->llvmInstructions_.back()->getParent();
            }
        }
    }
    return nullptr;
}

std::string Block::dotName() const {
    std::stringstream stream;
    stream << "NODE" << this;
    return stream.str();
}

std::string Block::label() const {
    std::string label_ = "[label=\"";
    label_ += "Function: ";
    label_ += llvmBlock()->getParent()->getName();
    label_ += "\\n\\nid:";
    label_ += std::to_string(traversalId_);
    if (isCallReturn()) {
        label_ += " Call Return Block\\n\\n";
    } else if (isArtificial()) {
        label_ += " Unified Exit Block\\n\\n";
    } else {
        label_ += " Block\\n\\n";
        std::string llvmTemporaryString;
        llvm::raw_string_ostream llvmStream(llvmTemporaryString);
        for (auto instruction : llvmInstructions_) {
            instruction->print(llvmStream);
            label_ += llvmTemporaryString + "\\n";
            llvmTemporaryString.clear();
        }
    }
    label_ += "\", shape=box]";
    return label_;
}

void Block::visit() {
    this->traversalId();
    for (auto successor : successors()) {
        if (successor->bfsId() == 0) {
            successor->visit();
        }
    }
    this->traversalId();
}

void Block::dumpNode(std::ostream &ostream) const {
    ostream << dotName() << " " << label();
}

void Block::dumpEdges(std::ostream &ostream) const {
    for (auto successor : successors()) {
        ostream << this->dotName() << " -> " << successor->dotName() << "\n";
    }

    for (auto callee : callees_) {
        ostream << this->dotName() << " -> " << callee.second->entry()->dotName() << " [style=dashed, constraint=false]\n";
        ostream << callee.second->exit()->dotName() << " -> " << this->dotName() << " [style=dashed, constraint=false]\n";
    }

    for (auto fork : forks_) {
        ostream << this->dotName() << " -> " << fork.second->entry()->dotName() << " [style=dotted, constraint=false]\n";
    }

    for (auto join : joins_) {
        ostream << join.second->exit()->dotName() << " -> " << this->dotName() << " [style=dotted, constraint=false]\n";
    }
}


}
}
