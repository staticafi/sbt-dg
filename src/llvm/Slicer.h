#ifndef _LLVM_DG_SLICER_H_
#define _LLVM_DG_SLICER_H_

#include <llvm/IR/Constants.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/CFG.h>
#include <llvm/Support/raw_ostream.h>

#include "analysis/Slicing.h"
#include "LLVMDependenceGraph.h"
#include "LLVMNode.h"

namespace dg {

class LLVMNode;

class LLVMSlicer : public analysis::Slicer<LLVMNode>
{
public:
    LLVMSlicer(){}

    void keepFunctionUntouched(const char *n)
    {
        dont_touch.insert(n);
    }

    /* virtual */
    bool removeNode(LLVMNode *node)
    {
        using namespace llvm;

        Value *val = const_cast<Value *>(node->getKey());
        // if there are any other uses of this value,
        // just replace them with undef
        val->replaceAllUsesWith(UndefValue::get(val->getType()));

        Instruction *Inst = dyn_cast<Instruction>(val);
        if (Inst) {
            Inst->eraseFromParent();
        } else {
            GlobalVariable *GV = dyn_cast<GlobalVariable>(val);
            if (GV)
                GV->eraseFromParent();
        }

        return true;
    }

    static void
    adjustPhiNodes(llvm::BasicBlock *pred, llvm::BasicBlock *blk)
    {
        using namespace llvm;

        for(Instruction& I : *pred) {
            PHINode *phi = dyn_cast<PHINode>(&I);
            if (phi) {
                // don't try remove block that we already removed
                int idx = phi->getBasicBlockIndex(blk);
                if (idx < 0)
                    continue;

                // the second argument is DeletePHIIFEmpty.
                // We don't want that, since that would make
                // dependence graph inconsistent. We'll
                // slice it away later, if it's empty
                phi->removeIncomingValue(idx, false);
            } else {
                // phi nodes are always at the beginning of the block
                // so if this is the first value that is not PHI,
                // there won't be any other and we can bail out
                break;
            }
        }
    }

    /* virtual */
    void removeBlock(LLVMBBlock *block)
    {
        assert(block);

        const llvm::Value *val = block->getKey();
        if (val == nullptr)
            return;

        llvm::Value *llvmval = const_cast<llvm::Value *>(val);
        llvm::BasicBlock *blk = llvm::cast<llvm::BasicBlock>(llvmval);

        for (auto succ : block->successors()) {
            if (succ.label == 255)
                continue;

            // don't adjust phi nodes in this block if this is a self-loop,
            // we're gonna remove the block anyway
            if (succ.target == block)
                continue;

            llvm::Value *sval = const_cast<llvm::Value *>(succ.target->getKey());
            if (sval)
                adjustPhiNodes(llvm::cast<llvm::BasicBlock>(sval), blk);
        }

        blk->eraseFromParent();
    }

    // override slice method
    uint32_t slice(LLVMNode *start, uint32_t sl_id = 0)
    {
        (void) sl_id;
        (void) start;

        assert(0 && "Do not use this method with LLVM dg");
        return 0;
    }

    uint32_t slice(LLVMDependenceGraph *maindg,
                   LLVMNode *start, uint32_t sl_id = 0)
    {
        // mark nodes for slicing
        assert(start || sl_id != 0);
        if (start)
            sl_id = mark(start, sl_id);

        // take every subgraph and slice it intraprocedurally
        // this includes the main graph
        extern std::map<const llvm::Value *,
                        LLVMDependenceGraph *> constructedFunctions;
        for (auto it : constructedFunctions) {
            if (dontTouch(it.first->getName()))
                continue;

            LLVMDependenceGraph *subdg = it.second;
            sliceGraph(subdg, sl_id);
        }

        return sl_id;
    }

private:
    void sliceCallNode(LLVMNode *callNode,
                       LLVMDependenceGraph *graph, uint32_t slice_id)
    {
        LLVMDGParameters *actualparams = callNode->getParameters();
        LLVMDGParameters *formalparams = graph->getParameters();

        /*
        if (!actualparams) {
            assert(!formalparams && "Have only one of params");
            return; // no params - nothing to do
        }

        assert(formalparams && "Have only one of params");
        assert(formalparams->size() == actualparams->size());
        */

        // FIXME slice arguments away
    }

    void sliceCallNode(LLVMNode *callNode, uint32_t slice_id)
    {
        for (LLVMDependenceGraph *subgraph : callNode->getSubgraphs())
            sliceCallNode(callNode, subgraph, slice_id);
    }

    static inline bool shouldSliceInst(const llvm::Value *val)
    {
        using namespace llvm;
        const Instruction *Inst = dyn_cast<Instruction>(val);
        if (!Inst)
            return true;

        switch (Inst->getOpcode()) {
            case Instruction::Unreachable:
#if 0
            case Instruction::Br:
            case Instruction::Switch:
            case Instruction::Ret:
#endif
                return false;
            default:
                return true;
        }
    }

/*
    static LLVMBBlock *
    createNewExitBB(LLVMDependenceGraph *graph)
    {
        using namespace llvm;

        LLVMBBlock *exitBB = new LLVMBBlock();

        Module *M = graph->getModule();
        LLVMContext& Ctx = M->getContext();
        BasicBlock *block = BasicBlock::Create(Ctx, "safe_exit");
        // terminate the basic block with unreachable
        // (we'll call _exit() before that)
        UnreachableInst *UI = new UnreachableInst(Ctx, block);
        Type *size_t_Ty;

        // call the _exit function
        Constant *Func = M->getOrInsertFunction("_exit",
                                                Type::getVoidTy(Ctx),
                                                Type::getInt32Ty(Ctx),
                                                NULL);

        std::vector<Value *> args;
        args.push_back(ConstantInt::get(Type::getInt32Ty(Ctx), 0));
        CallInst *CI = CallInst::Create(Func, args);
        CI->insertBefore(UI);

        Value *fval = const_cast<Value *>(graph->getEntry()->getKey());
        Function *F = cast<Function>(fval);
        F->getBasicBlockList().push_back(block);

        exitBB->append(new LLVMNode(CI));
        exitBB->append(new LLVMNode(UI));
        exitBB->setKey(block);
        exitBB->setDG(graph);

        return exitBB;
    }
*/

    static LLVMBBlock *
    createNewExitBB(LLVMDependenceGraph *graph)
    {
        using namespace llvm;

        LLVMBBlock *exitBB = new LLVMBBlock();

        Module *M = graph->getModule();
        LLVMContext& Ctx = M->getContext();
        BasicBlock *block = BasicBlock::Create(Ctx, "safe_return");

        Value *fval = const_cast<Value *>(graph->getEntry()->getKey());
        Function *F = cast<Function>(fval);
        F->getBasicBlockList().push_back(block);

        // fill in basic block just with return value
        ReturnInst *RI;
        if (F->getReturnType()->isVoidTy())
            RI = ReturnInst::Create(Ctx, block);
        else if (F->getName().equals("main"))
            // if this is main, than the safe exit equals to returning 0
            // (it is just for convenience, we wouldn't need to do this)
            RI = ReturnInst::Create(Ctx,
                                    ConstantInt::get(Type::getInt32Ty(Ctx), 0),
                                    block);
        else
            RI = ReturnInst::Create(Ctx, UndefValue::get(F->getReturnType()), block);

        exitBB->append(new LLVMNode(RI));
        exitBB->setKey(block);
        exitBB->setDG(graph);

        return exitBB;
    }

    static LLVMBBlock* addNewExitBB(LLVMDependenceGraph *graph)
    {

        // FIXME: don't create new one, create it
        // when creating graph and just use that one
        LLVMBBlock *newExitBB = createNewExitBB(graph);
        graph->setExitBB(newExitBB);
        graph->setExit(newExitBB->getLastNode());
        graph->addBlock(newExitBB->getKey(), newExitBB);

        return newExitBB;
    }

    // when we sliced away a branch of CFG, we need to reconnect it
    // to exit block, since on this path we would silently terminate
    // (this path won't have any effect on the property anymore)
    void makeGraphComplete(LLVMDependenceGraph *graph, uint32_t slice_id)
    {
        LLVMBBlock *oldExitBB = graph->getExitBB();
        assert(oldExitBB && "Don't have exit BB");

        LLVMBBlock *newExitBB = nullptr;

        for (auto it : graph->getBlocks()) {
            const llvm::BasicBlock *llvmBB
                = llvm::cast<llvm::BasicBlock>(it.first);
            const llvm::TerminatorInst *tinst = llvmBB->getTerminator();
            LLVMBBlock *BB = it.second;

            // nothing to do
            if (BB->successorsNum() == 0)
                continue;

            // if the BB has only one successor and the terminator
            // instruction is going to be sliced away, it means that
            // this is going to be an unconditional jump,
            // so just make the label 0
            if (BB->successorsNum() == 1
                && BB->getLastNode()->getSlice() != slice_id) {
                auto edge = *(BB->successors().begin());

                edge.label = 0;

                if (edge.target == oldExitBB) {
                     if (!newExitBB) {
                        newExitBB = addNewExitBB(graph);
                        oldExitBB->remove();
                    }

                    edge.target = newExitBB;
                }

                continue;
            }

            // when we have more successors, we need to fill in
            // jumps under labels that we sliced away

            DGContainer<uint8_t> labels;
            // go through BBs successors and gather all labels
            // from edges that go from this BB. Also if there's
            // a jump to return block, replace it with new
            // return block
            for (auto succ : BB->successors()) {
                // skip artificial return basic block
                if (succ.label == 255)
                    continue;

                // we have normal (not 255) label to exit node?
                // replace it with call to exit, because that means
                // that some path, that normally returns, was sliced
                // away and so if we're on this path, we won't affect
                // behaviour of slice - we can exit
                if (succ.target == oldExitBB) {
                    if (!newExitBB) {
                        newExitBB = addNewExitBB(graph);
                        oldExitBB->remove();
                    }

                    succ.target = newExitBB;
                } else
                    labels.insert(succ.label);
            }

            // replace missing labels. Label should be from 0 to some max,
            // no gaps, so jump to safe exit under missing labels
            for (uint8_t i = 0; i < tinst->getNumSuccessors(); ++i) {
                if (labels.contains(i))
                    continue;
                else {
                     if (!newExitBB) {
                        newExitBB = addNewExitBB(graph);
                        oldExitBB->remove();
                    }

                    bool ret = BB->addSuccessor(newExitBB, i);
                    assert(ret && "Already had this CFG edge, that is wrong");
                }
            }

            // if we have all successor edges pointing to the same
            // block, replace them with one successor (thus making
            // unconditional jump)
            if (BB->successorsNum() > 1 && BB->successorsAreSame()) {
                LLVMBBlock *succ = BB->successors().begin()->target;

                BB->removeSuccessors();
                BB->addSuccessor(succ, 0);
#ifdef ENABLE_DEBUG
                assert(BB->successorsNum() == 1
                       && "BUG: in removeSuccessors() or addSuccessor()");
#endif
            }
        }
    }

    void sliceGraph(LLVMDependenceGraph *graph, uint32_t slice_id)
    {
        // first slice away bblocks that should go away
        sliceBBlocks(graph, slice_id);

        // make graph complete
        makeGraphComplete(graph, slice_id);

        // now slice away instructions from BBlocks that left
        for (auto I = graph->begin(), E = graph->end(); I != E;) {
            LLVMNode *n = I->second;
            // shift here, so that we won't corrupt the iterator
            // by deleteing the node
            ++I;

            // we added this node artificially and
            // we don't want to slice it away or
            // take any other action on it
            if (n == graph->getExit())
                continue;

            ++statistics.nodesTotal;

            // keep instructions like ret or unreachable
            // FIXME: if this is ret of some value, then
            // the value is undef now, so we should
            // replace it by void ref
            if (!shouldSliceInst(n->getKey()))
                continue;

            /*
            if (llvm::isa<llvm::CallInst>(n->getKey()))
                sliceCallNode(n, slice_id);
                */

            if (n->getSlice() != slice_id) {
                removeNode(n);
                graph->deleteNode(n);
                ++statistics.nodesRemoved;
            }
        }

        // create new CFG edges between blocks after slicing
        reconnectLLLVMBasicBlocks(graph);

        // if we sliced away entry block, our new entry block
        // may have predecessors, which is not allowed in the
        // LLVM
        ensureEntryBlock(graph);
    }

    bool dontTouch(const llvm::StringRef& r)
    {
        for (const char *n : dont_touch)
            if (r.equals(n))
                return true;

        return false;
    }

    void reconnectBBlock(LLVMBBlock *BB, llvm::BasicBlock *llvmBB)
    {
        using namespace llvm;

        TerminatorInst *tinst = llvmBB->getTerminator();
        assert((!tinst || BB->successorsNum() <= 2 || llvm::isa<llvm::SwitchInst>(tinst))
                && "BB has more than two successors (and it's not a switch)");

        if (!tinst) {
            // block has no terminator
            // It may occur for example if we have:
            //
            //   call error()
            //   br %exit
            //
            //  The br instruction has no meaning when error() abort,
            //  but if error is not marked as noreturn, then the br
            //  will be there and will get sliced, making the block
            //  unterminated. The same may happen if we remove unconditional
            //  branch inst

            LLVMContext& Ctx = llvmBB->getContext();
            Function *F = cast<Function>(llvmBB->getParent());
            bool create_return = true;

            if (BB->successorsNum() == 1) {
                const LLVMBBlock::BBlockEdge& edge = *(BB->successors().begin());
                if (edge.label != 255) {
                    // don't create return, we created branchinst
                    create_return = false;

                    const BasicBlock *csucc = cast<BasicBlock>(edge.target->getKey());
                    BasicBlock *succ = const_cast<BasicBlock *>(csucc);
                    BranchInst::Create(succ, llvmBB);
                }
            }

            if (create_return) {
                if (F->getReturnType()->isVoidTy())
                    ReturnInst::Create(Ctx, llvmBB);
                else if (F->getName().equals("main"))
                    // if this is main, than the safe exit equals to returning 0
                    // (it is just for convenience, we wouldn't need to do this)
                    ReturnInst::Create(Ctx,
                                       ConstantInt::get(Type::getInt32Ty(Ctx), 0),
                                       llvmBB);
                else
                    ReturnInst::Create(Ctx,
                                       UndefValue::get(F->getReturnType()), llvmBB);

            }

            // and that is all we can do here
            return;
        }

        for (const LLVMBBlock::BBlockEdge& succ : BB->successors()) {
            // skip artificial return basic block
            if (succ.label == 255)
                continue;

            llvm::Value *val = const_cast<llvm::Value *>(succ.target->getKey());
            assert(val && "nullptr as BB's key");
            llvm::BasicBlock *llvmSucc = llvm::cast<llvm::BasicBlock>(val);
            tinst->setSuccessor(succ.label, llvmSucc);
        }

        // if the block still does not have terminator
    }

    void reconnectLLLVMBasicBlocks(LLVMDependenceGraph *graph)
    {
        for (auto it : graph->getBlocks()) {
            llvm::BasicBlock *llvmBB
                = llvm::cast<llvm::BasicBlock>(const_cast<llvm::Value *>(it.first));
            LLVMBBlock *BB = it.second;

            reconnectBBlock(BB, llvmBB);
        }
    }

    void ensureEntryBlock(LLVMDependenceGraph *graph)
    {
        using namespace llvm;

        Value *val = const_cast<Value *>(graph->getEntry()->getKey());
        Function *F = cast<Function>(val);

        // Function is empty, just bail out
        if(F->begin() == F->end())
            return;

        BasicBlock *entryBlock = &F->getEntryBlock();

        if (pred_begin(entryBlock) == pred_end(entryBlock)) {
            // entry block has no predecessor, we're ok
            return;
        }

        // it has some predecessor, create new one, that will just
        // jump on it
        LLVMContext& Ctx = graph->getModule()->getContext();
        BasicBlock *block = BasicBlock::Create(Ctx, "single_entry");

        // jump to the old entry block
        BranchInst::Create(entryBlock, block);

        // set it as a new entry by pusing the block to the front
        // of the list
        F->getBasicBlockList().push_front(block);

        // FIXME: propagate this change to dependence graph
    }

    // do not slice these functions at all
    std::set<const char *> dont_touch;
};
} // namespace dg

#endif  // _LLVM_DG_SLICER_H_

