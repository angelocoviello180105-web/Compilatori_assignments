#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Constants.h"
#include <vector>

using namespace llvm;

namespace{

struct AlgebraicIdentityPass: PassInfoMixin<AlgebraicIdentityPass>{

    PreservedAnalyses run(Function &F, FunctionAnalysisManager &){
        // vettore delle istruzioni da eliminare
        std::vector<Instruction*> instructionsToErase;

        for(auto &B: F){
            for(auto &I: B){
                // l'operazione è binaria?
                if(auto *binOp = dyn_cast<BinaryOperator>(&I)){
                    // 1. addizione (x + 0 = x)
                    if(binOp->getOpcode() == Instruction::Add){
                        // estrazione degli operandi
                        Value *op0 = binOp->getOperand(0);
                        Value *op1 = binOp->getOperand(1);

                        // estrazione della costante (se presente) 
                        ConstantInt *C = nullptr;
                        // memorizzazione dell'altro operando
                        Value *otherOp = nullptr;

                        // quale dei due operandi è la costante?
                        if((C = dyn_cast<ConstantInt>(op0)))
                            otherOp = op1;
                        else if((C = dyn_cast<ConstantInt>(op1)))
                            otherOp = op0;

                        // la costante è = 0?
                        if(C && C->isZero()){
                            outs() << "\nADD rimossa per Algebraic Identity: " << *binOp
                                << "\nSostituita con l'istruzione: " << *otherOp << "\n";

                            // sostituzione degli usi del risultato dell'ADD con l'altro operando
                            binOp->replaceAllUsesWith(otherOp);

                            // si segna l'istruzione per l'eliminazione
                            instructionsToErase.push_back(binOp);
                        }
                    }
                    // 2. moltiplicazione (x * 1 = x)
                    else if(binOp->getOpcode() == Instruction::Mul){
                        // estrazione degli operandi
                        Value *op0 = binOp->getOperand(0);
                        Value *op1 = binOp->getOperand(1);

                        // estrazione della costante (se presente) 
                        ConstantInt *C = nullptr;
                        // memorizzazione dell'altro operando
                        Value *otherOp = nullptr;

                        // quale dei due operandi è la costante?
                        if((C = dyn_cast<ConstantInt>(op0)))
                            otherOp = op1;
                        else if((C = dyn_cast<ConstantInt>(op1)))
                            otherOp = op0;

                        // la costante è = 1?
                        if(C && C->isOne()){
                            outs() << "\nMUL rimossa per Algebraic Identity: " << *binOp
                                << "\nSostituita con l'istruzione: " << *otherOp << "\n";

                            // sostituzione degli usi del risultato della MUL con l'altro operando
                            binOp->replaceAllUsesWith(otherOp);

                            // si segna l'istruzione per l'eliminazione
                            instructionsToErase.push_back(binOp);
                        }
                    }
                }
            }
        }

        // se non c'è nulla da cancellare, l'IR non è stato toccato
        if(instructionsToErase.empty())
            return PreservedAnalyses::all();

        // cancellazione delle istruzioni rese inutili dall'ottimizzazione
        for(Instruction *I: instructionsToErase)
            I->eraseFromParent();

        // l'IR è stato modificato, ma non la struttura dei blocchi (CFG)
        PreservedAnalyses PA;
        PA.preserveSet<CFGAnalyses>();
        return PA;
    }


    static bool isRequired(){
        return true;
    }
};
}


llvm::PassPluginLibraryInfo getAlgebraicIdentityPassPluginInfo(){
    return{ LLVM_PLUGIN_API_VERSION, "AlgebraicIdentityPass", LLVM_VERSION_STRING, [](PassBuilder &PB){
        PB.registerPipelineParsingCallback(
            [](StringRef Name, FunctionPassManager &FPM, ArrayRef<PassBuilder::PipelineElement>){
                if(Name == "AlgebraicIdentityPass"){
                    FPM.addPass(AlgebraicIdentityPass());
                    return true;
                }
                return false;
            }
        );
    }};
}


extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo(){
    return getAlgebraicIdentityPassPluginInfo();
}