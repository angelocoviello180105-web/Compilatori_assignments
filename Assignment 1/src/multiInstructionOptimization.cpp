#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"
#include <vector>

using namespace llvm::PatternMatch;
using namespace llvm;

namespace{

struct MultiInstructionOptimizationPass: PassInfoMixin<MultiInstructionOptimizationPass>{

    PreservedAnalyses run(Function &F, FunctionAnalysisManager &){
        // vettore delle istruzioni da eliminare
        std::vector<Instruction*> instructionsToErase;

        for(auto &B: F){
            for(auto &I: B){
                // l'operazione è binaria?
                if(isa<BinaryOperator>(&I)){
                    Value *operand1 = nullptr;
                    ConstantInt *C1 = nullptr;

                    /* 1. l'operazione è un'ADD e il secondo operando è una costante?
                    pattern: (x - c) + c = x */
                    if(match(&I, m_Add(m_Value(operand1), m_ConstantInt(C1)))){
                        Value *operand2 = nullptr;
                        ConstantInt *C2 = nullptr;

                        // il primo operando dell'ADD è una SUB con una costante?
                        if(match(operand1, m_Sub(m_Value(operand2), m_ConstantInt(C2)))){
                            // le due costanti hanno lo stesso valore?
                            if(C1->getValue() == C2->getValue()){
                                outs() << "\nADD rimossa per Multi-Instruction Optimization: " << I
                                    << "\nSostituita con il valore: " << *operand2 << "\n";

                                // sostituzione degli usi con l'operando originale (x)
                                I.replaceAllUsesWith(operand2);

                                // si segna l'istruzione (ADD esterna) per l'eliminazione
                                instructionsToErase.push_back(&I);
                            }
                        }
                    }
                    /* 2. l'operazione è una SUB e il secondo operando è una costante?
                    pattern: (x + c) - c = x */
                    else if(match(&I, m_Sub(m_Value(operand1), m_ConstantInt(C1)))){
                        Value *operand2 = nullptr;
                        ConstantInt *C2 = nullptr;

                        // il primo operando della SUB è un'ADD con una costante?
                        if(match(operand1, m_Add(m_Value(operand2), m_ConstantInt(C2)))){
                            // le due costanti hanno lo stesso valore?
                            if(C1->getValue() == C2->getValue()){
                                outs() << "\nSUB rimossa per Multi-Instruction Optimization: " << I
                                    << "\nSostituita con il valore: " << *operand2 << "\n";

                                // sostituzione degli usi con l'operando originale (x)
                                I.replaceAllUsesWith(operand2);

                                // si segna l'istruzione (SUB esterna) per l'eliminazione
                                instructionsToErase.push_back(&I);
                            }
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


llvm::PassPluginLibraryInfo getMultiInstructionOptimizationPassPluginInfo(){
    return{ LLVM_PLUGIN_API_VERSION, "MultiInstructionOptimizationPass", LLVM_VERSION_STRING, [](PassBuilder &PB){
        PB.registerPipelineParsingCallback(
            [](StringRef Name, FunctionPassManager &FPM, ArrayRef<PassBuilder::PipelineElement>){
                if(Name == "MultiInstructionOptimizationPass"){
                    FPM.addPass(MultiInstructionOptimizationPass());
                    return true;
                }
                return false;
            }
        );
    }};
}


extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo(){
    return getMultiInstructionOptimizationPassPluginInfo();
}