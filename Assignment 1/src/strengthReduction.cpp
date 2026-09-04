#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Constants.h"
#include <vector>
#include <cmath>

using namespace llvm;

namespace{

struct StrengthReductionPass: PassInfoMixin<StrengthReductionPass>{

    PreservedAnalyses run(Function &F, FunctionAnalysisManager &){
        // vettore delle istruzioni da eliminare
        std::vector<Instruction*> instructionsToErase;

        for(auto &B: F){
            for(auto &I: B){
                // l'operazione è binaria?
                if(auto *binOp = dyn_cast<BinaryOperator>(&I)){
                    // 1. è una moltiplicazione?
                    if(binOp->getOpcode() == Instruction::Mul){
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

                        if(C){
                            Instruction *shiftLeftInstruction = nullptr;

                            // puntatore all'ultima istruzione per poterne sostituire gli usi
                            Instruction *lastInstruction = nullptr;

                            // si controlla se ci si trova al più a distanza 2 da una potenza di 2
                            std::vector<int> distances = {0, -1, 1, -2, 2};

                            for(int i: distances){
                                // cast a uint64_t per evitare warning del compilatore
                                APInt value = C->getValue() + (uint64_t)i;

                                // è una potenza di due?
                                if(value.isPowerOf2()){
                                    // calcolo del #bit da shiftare
                                    unsigned bitToShift = value.exactLogBase2();

                                    // creazione dell'operazione di shift sx
                                    shiftLeftInstruction = BinaryOperator::Create(
                                        Instruction::Shl, otherOp,
                                        ConstantInt::get(C->getType(), bitToShift)
                                    );

                                    shiftLeftInstruction->insertAfter(binOp);
                                    lastInstruction = shiftLeftInstruction;

                                    // la costante iniziale non è multiplo di 2; se i!=0, si effettuano ADD o SUB
                                    for(int j=0; j<std::abs(i); j++){
                                        // se i>0 il multiplo di 2 più vicino è dopo, quindi si sottrae l'eccesso
                                        if(i > 0){
                                            Instruction *subInstruction = BinaryOperator::Create(
                                                Instruction::Sub, lastInstruction, otherOp
                                            );

                                            subInstruction->insertAfter(lastInstruction);
                                            lastInstruction = subInstruction;
                                        }
                                        // se i<0 il multiplo di 2 più vicino è prima, quindi si somma la rimanenza
                                        else{
                                            Instruction *addInstruction = BinaryOperator::Create(
                                                Instruction::Add, lastInstruction, otherOp
                                            );

                                            addInstruction->insertAfter(lastInstruction);
                                            lastInstruction = addInstruction;
                                        }
                                    }

                                    break;
                                }
                            }

                            // lastInstruction!=nullptr se è stato trovato un multiplo di 2 a distanza <=2
                            if(lastInstruction){
                                outs() << "\nMUL rimossa: " << *binOp
                                    << "\nSostituita con la sequenza di istruzioni:\n\t"
                                    << *shiftLeftInstruction << "\n";

                                if(lastInstruction != shiftLeftInstruction)
                                    outs() << "\t" << *lastInstruction << "\n";

                                // si rimpiazzano gli usi dell'operazione MUL originale
                                binOp->replaceAllUsesWith(lastInstruction);

                                // si segna l'istruzione originale per l'eliminazione
                                instructionsToErase.push_back(binOp);
                            }
                        }
                    }
                    // 2. l'operazione è una divisione con segno?
                    else if(binOp->getOpcode() == Instruction::SDiv){
                        // estrazione degli operandi
                        Value *op0 = binOp->getOperand(0);
                        Value *op1 = binOp->getOperand(1);

                        // memorizzazione della costante (se presente)
                        ConstantInt *C = dyn_cast<ConstantInt>(op1);
                        // memorizzazione dell'altro operando
                        Value *otherOp = op0;

                        // la costante esiste ed è una potenza di 2?
                        if(C && C->getValue().isPowerOf2()){
                            // calcolo del #bit da shiftare
                            unsigned bitToShift = C->getValue().exactLogBase2();

                            // operazione di shift dx
                            Instruction *shiftRightInstruction = BinaryOperator::Create(
                                /* SDiv (divisione C++) arrotonda verso 0: -3/2 = -1
                                AShr (shift aritmetico) arrotonda verso -inf: -3 >> 1 = -2 */
                                Instruction::AShr, otherOp,
                                ConstantInt::get(C->getType(), bitToShift)
                            );

                            shiftRightInstruction->insertAfter(binOp);

                            outs() << "\nDIV rimossa: " << *binOp << "\nSostituita con l'istruzione: "
                                << *shiftRightInstruction << "\n";

                            // si rimpiazzano gli usi dell'operazione
                            binOp->replaceAllUsesWith(shiftRightInstruction);

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


llvm::PassPluginLibraryInfo getStrengthReductionPassPluginInfo(){
    return{ LLVM_PLUGIN_API_VERSION, "StrengthReductionPass", LLVM_VERSION_STRING, [](PassBuilder &PB){
        PB.registerPipelineParsingCallback(
            [](StringRef Name, FunctionPassManager &FPM, ArrayRef<PassBuilder::PipelineElement>){
                if (Name == "StrengthReductionPass"){
                    FPM.addPass(StrengthReductionPass());
                    return true;
                }
                return false;
            }
        );
    }};
}


extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo(){
    return getStrengthReductionPassPluginInfo();
}