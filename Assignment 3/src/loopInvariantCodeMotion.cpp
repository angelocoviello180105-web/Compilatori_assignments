#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include <vector>

using namespace llvm;

namespace{

struct LoopInvariantCodeMotionPass: PassInfoMixin<LoopInvariantCodeMotionPass>{

    bool checkAllExitsDominated(Instruction *I, DominatorTree &DT, SmallVector<BasicBlock*, 4> &exitBBs){
        BasicBlock *BB = I->getParent();

        for(BasicBlock *exitBB: exitBBs)
            // esiste almeno un'uscita non dominata dal BB?
            if(!DT.dominates(BB, exitBB))
                return false;

        return true;
    }


    // un'istruzione è dead dopo il loop se il suo risultato non viene mai usato al di fuori del ciclo
    bool isDeadOutside(Instruction *I, Loop *L){
        // si analizzano tutti gli users dell'istruzione
        for(User *U: I->users()){
            // lo user è un'istruzione?
            if(auto *userInst = dyn_cast<Instruction>(U)){
                BasicBlock *BB = userInst->getParent();
                // se il blocco in cui si trova l'user non è nel loop, il valore sta venendo letto all'esterno
                if(!L->contains(BB))
                    return false;
            }
        }
        return true;
    }


    bool isMovable(Instruction *I, Loop *L, DominatorTree &DT, SmallVector<BasicBlock*, 4> &exitBBs){
        // un'istruzione è movable se domina tutte le uscite o se è dead fuori dal loop
        return checkAllExitsDominated(I, DT, exitBBs) || isDeadOutside(I, L);
    }


    // restituisce il #istruzioni spostate
    int moveInstructions(std::vector<Instruction*> &movableInstructions, BasicBlock *preheader, Loop *L){
        // inizializzazione della worklist con i candidati
        std::vector<Instruction*> worklist = movableInstructions;
        bool changed = true;
        int movedCount = 0;

        // il ciclo prosegue finché si riesce a spostare almeno un'istruzione
        while(changed && !worklist.empty()){
            changed = false;
            auto it = worklist.begin();

            while(it != worklist.end()){
                Instruction *I = *it;
                bool readyToMove = true;

                // ispezione degli operandi
                for(Value *op: I->operands()){
                    if(Instruction *defI = dyn_cast<Instruction>(op)){
                        // se l'istruzione da cui dipende si trova nel ciclo, lo spostamento è bloccato.
                        /* se 'defI' fosse già stata spostata al giro precedente, il suo Parent
                            sarebbe diventato il preheader, rendendo false la condizione */
                        if(L->contains(defI->getParent())){
                            readyToMove = false;
                            break;
                        }
                    }
                }

                // le dipendenze sono soddisfatte?
                if(readyToMove){
                    I->moveBefore(preheader->getTerminator());

                    // rimozione dalla worklist
                    it = worklist.erase(it); 
                    outs() << "\n\tL'istruzione " << *I << " è stata spostata correttamente\n";
                    changed = true;
                    movedCount++;
                }
                // l'istruzione non è ancora pronta, la si lascia in coda per la prossima iterazione
                else
                    it++;
            }
        }

        return movedCount;
    }


    // verifica ricorsivamente se i calcoli di un'istruzione sono invarianti rispetto alle iterazioni del loop
    bool isLoopInvariant(Instruction *I, Loop *L){
        /* per evitare side-effects, ci si limita ai BinaryOperator,
            escludendo terminatori e PHI node */
        if(!isa<BinaryOperator>(I) || I->isTerminator() || isa<PHINode>(I))
            return false;

        // si scorrono gli operandi dell'istruzione
        for(Value *op: I->operands())
            // l'operando è calcolato da un'altra istruzione?
            if(Instruction *defI = dyn_cast<Instruction>(op))
                // l'istruzione è contenuta nel loop e non è loop-invariant?
                if(L->contains(defI->getParent()) && !isLoopInvariant(defI, L))
                    return false;

        return true;
    }


    void runLICM(Loop *L, DominatorTree &DT){
        // il loop è in forma normale?
        if(L->isLoopSimplifyForm()){
            BasicBlock *preheader = L->getLoopPreheader();

            if(preheader != nullptr){
                // 1. individuazione delle istruzioni loop-invariant
                std::vector<Instruction*> loopInvariantInstructions;
                for(BasicBlock *BB: L->blocks())
                    for(Instruction &I: *BB)
                        if(isLoopInvariant(&I, L))
                            loopInvariantInstructions.push_back(&I);

                // recupero dei blocchi di uscita del loop
                SmallVector<BasicBlock*, 4> exitBBs;
                L->getExitBlocks(exitBBs);

                // 2. individuazione delle istruzioni movable
                std::vector<Instruction*> movableInstructions;
                for(Instruction *I: loopInvariantInstructions)
                    if(isMovable(I, L, DT, exitBBs))
                        movableInstructions.push_back(I);

                /* 3. spostamento delle istruzioni movable, se presenti,
                    rispettando l'ordine delle dipendenze */
                if(!movableInstructions.empty()){
                    outs() << "\nAnalisi del Loop " << L << "\n\tIstruzioni candidate allo spostamento: "
                        << movableInstructions.size() << "\n";

                    int moved = moveInstructions(movableInstructions, preheader, L);
                    outs() << "\n\tIstruzioni spostate nel preheader: " << moved << "\n";
                }
            }
        }
    }


    // processa i loop ricorsivamente con approccio bottom-up (post-order)
    void processLoop(Loop *L, DominatorTree &DT){
        // si processano prima i sotto-loop (quelli più annidati)
        for(Loop *subL: L->getSubLoops())
            processLoop(subL, DT);

        // poi si processa il loop corrente
        runLICM(L, DT);
    }


    PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM){
        // estrazione delle analisi necessarie
        DominatorTree &DT = AM.getResult<DominatorTreeAnalysis>(F);
        LoopInfo &LI = AM.getResult<LoopAnalysis>(F);

        // si scorrono e processano i loop della funzione
        for(auto &L: LI)
            processLoop(L, DT);

        /* spostando le istruzioni nel preheader si modifica l'IR, ma non si
            creano e/o distruggono blocchi base né si modificano i branch. */
        // di conseguenza, CFG e D-Tree rimangono validi
        PreservedAnalyses PA;
        PA.preserveSet<CFGAnalyses>();
        return PA;
    }


    static bool isRequired(){
        return true;
    }
};
}


llvm::PassPluginLibraryInfo getLoopInvariantCodeMotionPassPluginInfo(){
    return{ LLVM_PLUGIN_API_VERSION, "LoopInvariantCodeMotionPass", LLVM_VERSION_STRING, [](PassBuilder &PB){
        PB.registerPipelineParsingCallback(
            [](StringRef Name, FunctionPassManager &FPM, ArrayRef<PassBuilder::PipelineElement>){
                if(Name == "LoopInvariantCodeMotionPass"){
                    FPM.addPass(LoopInvariantCodeMotionPass());
                    return true;
                }
                return false;
            }
        );
    }};
}


extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo(){
    return getLoopInvariantCodeMotionPassPluginInfo();
}