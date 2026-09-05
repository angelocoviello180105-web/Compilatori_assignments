#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include <algorithm>
#include <utility>

using namespace llvm;

namespace{

struct LoopFusionPass: PassInfoMixin<LoopFusionPass>{

	BasicBlock *getLogicalEntry(Loop *L){
		// il loop è guarded? --> il punto di ingresso logico è la guardia, altrimenti è l'header
        if(L->isGuarded())
            return L->getLoopGuardBranch()->getParent();
        return L->getHeader();
    }


	bool isInductionVariables(Loop *L0, Loop *L1){
		PHINode *IV0 = L0->getCanonicalInductionVariable();
        PHINode *IV1 = L1->getCanonicalInductionVariable();

		if(!IV0 || !IV1)
			return false;
		return true;
	}


	bool detectNegativeDependencies(Loop *L0, Loop *L1, DependenceInfo &DI, ScalarEvolution &SE){
		// si analizzano tutte le istruzioni del primo loop
		for(BasicBlock *BB0: L0->blocks()){
			for(Instruction &I0: *BB0){
				// le dipendenze interessano solo istruzioni che leggono o scrivono memoria
				if(!I0.mayReadOrWriteMemory())
					continue;

				for(BasicBlock *BB1: L1->blocks()){
					for(Instruction &I1: *BB1){
						if(!I1.mayReadOrWriteMemory())
							continue;

						// esiste una dipendenza tra I0 e I1?
                    	// 'true' --> indica a LLVM di cercare dipendenze anche se i loop sono disgiunti
						auto dep = DI.depends(&I0, &I1, true);

						// non esiste una dipendenza? --> nulla da controllare
						// esiste una dipendenza di tipo Load-Load? --> nulla da controllare
						if(!dep || dep->isInput())
							continue;

						// estrazione dei puntatori di memoria della dipendenza
                        Value *ptr0 = nullptr;
                        if(auto *LoadIntruction = dyn_cast<LoadInst>(&I0))
							ptr0 = LoadIntruction->getPointerOperand();
                        else if(auto *StoreInstruction = dyn_cast<StoreInst>(&I0))
							ptr0 = StoreInstruction->getPointerOperand();

						Value *ptr1 = nullptr;
                        if(auto *LoadIntruction = dyn_cast<LoadInst>(&I1))
							ptr1 = LoadIntruction->getPointerOperand();
                        else if (auto *StoreInstruction = dyn_cast<StoreInst>(&I1))
							ptr1 = StoreInstruction->getPointerOperand();

						// istruzione sconosciuta? --> interruzione dell'analisi
						if(!ptr0 || !ptr1)
							return true; 

						// calcolo della distanza della dipendenza tra i due puntatori
						const SCEV *scev0 = SE.getSCEV(ptr0);
						const SCEV *scev1 = SE.getSCEV(ptr1);

						/* espressioni di ricorrenza additiva
							1. Start (offset iniziale)
							2. Step (incremento ad ogni iterazione)
						--> Valore = Start + (Iterazione * Step) */
						auto *addRec0 = dyn_cast<SCEVAddRecExpr>(scev0);
                        auto *addRec1 = dyn_cast<SCEVAddRecExpr>(scev1);

						// gli accessi seguono un pattern lineare prevedibile?
						if(addRec0 && addRec1){
							// estrazione dell'offset iniziale di entrambi gli accessi
                            const SCEV *start0 = addRec0->getStart();
                            const SCEV *start1 = addRec1->getStart();
							const SCEV *distance = SE.getMinusSCEV(start0, start1);

							if(auto *CDistance = dyn_cast<SCEVConstant>(distance)){
								/* differenza negativa? --> L1 sta leggendo un offset
									che L0 scriverà in un'iterazione successiva. */
								// la fusione altererebbe la semantica del programma
								if(CDistance->getValue()->isNegative())
									return true;
							}
							else
								// la distanza non è costante? --> blocco preventivo della fusione
								return true;
						}
						else
							// gli accessi non sono lineari? --> blocco preventivo della fusione
							return true;
					}
				}
			}
		}

		return false;
	}


	bool compareTripCounts(Loop *L0, Loop *L1, ScalarEvolution &SE){
    	const SCEV *L0Backedge = SE.getBackedgeTakenCount(L0);
    	const SCEV *L1Backedge = SE.getBackedgeTakenCount(L1);

		// se ScalarEvolution non è in grado di calcolare il trip count, restituisce SCEVCouldNotCompute
		if(L0Backedge == L1Backedge && !isa<SCEVCouldNotCompute>(L0Backedge))
			return true;
		return false;
	}


	bool isCFE(Loop *L0, Loop *L1, DominatorTree &DT, PostDominatorTree &PDT, ScalarEvolution &SE){
		bool L0Guarded = L0->isGuarded();
		bool L1Guarded = L1->isGuarded();

    	// 1. i loop sono entrambi guarded
		if(L0Guarded && L1Guarded){
			auto *L0GuardBranch = L0->getLoopGuardBranch();
			auto *L1GuardBranch = L1->getLoopGuardBranch();

			// controllo di dominanza sui BB delle guardie
			if(!DT.dominates(L0GuardBranch->getParent(), L1GuardBranch->getParent()) || !PDT.dominates(L1GuardBranch->getParent(), L0GuardBranch->getParent()))
				return false;

			Value *cond0 = L0GuardBranch->getCondition();
			Value *cond1 = L1GuardBranch->getCondition();
			// le condizioni coincidono?
			/* ScalarEvolution risolve problemi dove nel codice m2r non è presente l'istruzione di ICmp,
				ad es. 'if(condition)' invece di 'if(condition == 1)' */
			if(SE.getSCEV(cond0) == SE.getSCEV(cond1))
				return true;

			// si prova un cast a ICmp delle due condizioni
			auto *cmp0 = dyn_cast<ICmpInst>(cond0);
			auto *cmp1 = dyn_cast<ICmpInst>(cond1);
			if(!cmp0 || !cmp1)
				return false;

			// ScalarEvolution degli operandi delle operazioni di confronto
			const SCEV *LHS0 = SE.getSCEV(cmp0->getOperand(0));
			const SCEV *RHS0 = SE.getSCEV(cmp0->getOperand(1));
			const SCEV *LHS1 = SE.getSCEV(cmp1->getOperand(0));
			const SCEV *RHS1 = SE.getSCEV(cmp1->getOperand(1));

			auto pred0 = cmp0->getPredicate();
			auto pred1 = cmp1->getPredicate();
			// i confronti sono uguali?
			if(pred0==pred1 && LHS0==LHS1 && RHS0==RHS1)
				return true;

			// calcolo del predicato equivalente se gli operandi del confronto vengano scambiati.
			/* utile per confrontare condizioni logicamente equivalenti, ad es. 'x < y' e 'y > x',
				portandole nella stessa forma canonica */
			pred1 = ICmpInst::getSwappedPredicate(cmp1->getPredicate());
			if(pred0==pred1 && LHS0==RHS1 && RHS0==LHS1)
				return true;
		}
		// 2. i loop sono entrambi non guarded
		else if(!L0Guarded && !L1Guarded){
			BasicBlock *L0Header = L0->getHeader();
			BasicBlock *L1Header = L1->getHeader();

			// controllo di dominanza sui BB degli header
			if(DT.dominates(L0Header, L1Header) && PDT.dominates(L1Header, L0Header))
				return true;
		}

		return false;
	}


	bool checkAdjacency(Loop *L0, Loop *L1){
		bool L0Guarded = L0->isGuarded();
		bool L1Guarded = L1->isGuarded();

		// 1. i loop sono entrambi guarded
		if(L0Guarded && L1Guarded){
			// branch delle due guardie
			auto L0GuardBranch = L0->getLoopGuardBranch();
			auto L1GuardBranch = L1->getLoopGuardBranch();

			BasicBlock *L0Preheader = L0->getLoopPreheader();
			BasicBlock *L0Bypass = nullptr;
			BasicBlock *L1GuardBlock = L1GuardBranch->getParent();

			/* identificazione del bypass block, dato che la guardia di L0 ha due successori:
				- uno entra nel loop (preheader)
				- uno salta il loop (bypass) */
			if(L0GuardBranch->getSuccessor(0) == L0Preheader)
				L0Bypass = L0GuardBranch->getSuccessor(1);
			else
				L0Bypass = L0GuardBranch->getSuccessor(0);

			/* se si salta L0 (tramite il bypass), si finisce sulla guardia di L1?
				no --> c'è del codice in mezzo --> i loop non sono adiacenti */
			if(L0Bypass != L1GuardBlock)
				return false;

			// gli exit blocks di L0 devono puntare alla guardia di L1
			SmallVector<BasicBlock *, 4> exitBlocks;
			L0->getExitBlocks(exitBlocks);
			for(BasicBlock *exit: exitBlocks){
				// l'uscita non é direttamente la guardia di L1?
				if(exit != L1GuardBlock){
					// si controlla che ci sia un salto incondizionato verso la guardia senza istruzioni nel mezzo
					auto *terminator = exit->getTerminator();
					if(terminator->getNumSuccessors()!=1 || terminator->getSuccessor(0)!=L1GuardBlock || exit->size()>1)
						return false;
				}
			}
			return true;
		}
		// 2. i loop sono entrambi non guarded
		else if (!L0Guarded && !L1Guarded){
			// exit blocks di L0
			SmallVector<BasicBlock *, 4> exitBlocks;
			L0->getExitBlocks(exitBlocks);

			// preheader di L1
			BasicBlock *preheader = L1->getLoopPreheader();

			// c'è un calcolo nel preheader? --> non adiacenti
			for(Instruction &I: *preheader)
				if(!isa<PHINode>(&I) && !isa<BranchInst>(&I))
					return false;

			// tutte le uscite devono puntare al preheader
			for(BasicBlock *exit: exitBlocks)
				if(exit != preheader)
					return false;

			return true;
		}

    	// la condizione di guardia dei loop é mista --> non si procede con l'ottimizzazione
		return false;
	}


	bool checkSimplifyForm(Loop *L0, Loop *L1){
        if(!L0->isLoopSimplifyForm()){
            outs() << "Loop 1 non è in forma normale\n";
            return false;
        }
        else if(!L1->isLoopSimplifyForm()){
            outs() << "Loop 2 non è in forma normale\n";
            return false;
        }

        return true;
    }


	std::vector<std::pair<Loop*, Loop*>> getLoopPairCandidates(const std::vector<Loop*> &loops, DominatorTree &DT, PostDominatorTree &PDT, ScalarEvolution &SE, DependenceInfo &DI){
		std::vector<std::pair<Loop*, Loop*>> loopPairCandidates;
		std::vector<Loop*> usedLoops;

		for(size_t i=0; i<loops.size()-1; ++i){
			// si salta se uno dei due loop è già stato inserito in una coppia precedente
			if(std::find(usedLoops.begin(), usedLoops.end(), loops[i])!=usedLoops.end() || std::find(usedLoops.begin(), usedLoops.end(), loops[i+1])!=usedLoops.end())
				continue;

			Loop *L0 = loops[i];
			Loop *L1 = loops[i+1];

			outs() << "\nCoppia candidata alla fusione:\n"
				<< "\tLoop 1: " << L0 << "\n"
				<< "\tLoop 2: " << L1 << "\n";

			if(!checkSimplifyForm(L0, L1))
				continue;

			if(!L0->getExitBlock() || !L1->getExitBlock()){
				outs() << "Uno dei loop non ha un unico exit block, impossibile fondere\n";
				continue;
			}

			if(!L0->getExitingBlock() || !L1->getExitingBlock()){
				outs() << "Uno dei loop non ha un unico exiting block, impossibile fondere\n";
				continue;
			}

			if(!checkAdjacency(L0, L1)){
				outs() << "I due loop non sono adiacenti\n";
				continue;
			}

			if(!isCFE(L0, L1, DT, PDT, SE)){
				outs() << "I due loop non sono control flow equivalent\n";
				continue;
			}

			if(!compareTripCounts(L0, L1, SE)){
				outs() << "I due loop non eseguono lo stesso numero di volte\n";
				continue;
			}

			if(detectNegativeDependencies(L0, L1, DI, SE)){
				outs() << "I due loop hanno istruzioni con dipendenza negativa\n";
				continue;
			}

			if(!isInductionVariables(L0, L1)){
				outs() << "Impossibile trovare le Induction Variables per entrambi i loop\n";
				continue;
			}

			// si aggiunge la coppia solo se supera tutti i controlli
			loopPairCandidates.push_back({L0, L1});

			// si segnano i due loop come "usati" per bloccare catene multiple
			usedLoops.push_back(L0);
			usedLoops.push_back(L1);
		}

		return loopPairCandidates;
	}


	void setTerminator(BasicBlock *BB0, BasicBlock *BB1, int successorNumber){
    	auto *branchInstruction = dyn_cast<BranchInst>(BB0->getTerminator());
		if(branchInstruction)
			branchInstruction->setSuccessor(successorNumber, BB1);
	}


	void linkLoopPair(Loop *L0, Loop *L1){
    	// body di L0 come predecessore del latch
    	BasicBlock *L0Latch = L0->getLoopLatch();
    	BasicBlock *L0BodyBlock = L0Latch->getSinglePredecessor();

    	// primo body block di L1 utilizzando il suo header
    	BasicBlock *L1Header = L1->getHeader();
    	BasicBlock *L1FirstBodyBlock = L1Header;
    	for(BasicBlock *succ: successors(L1Header)){
			if(L1->contains(succ) && succ != L1->getLoopLatch()){
				L1FirstBodyBlock = succ;
				break;
			}
		}

		// ultimo body block di L1 utilizzando il latch
    	BasicBlock *L1Latch = L1->getLoopLatch();
    	BasicBlock *L1LastBodyBlock = L1Latch->getSinglePredecessor();

		// collegamento dei vari componenti tra loro
		setTerminator(L0BodyBlock, L1FirstBodyBlock, 0);
		setTerminator(L1LastBodyBlock, L0Latch, 0);
	}


	void updateInductionVariables(Loop *L0, Loop *L1){
		PHINode *L0InductionVariable = L0->getCanonicalInductionVariable();
    	PHINode *L1InductionVariable = L1->getCanonicalInductionVariable();

		L1InductionVariable->replaceAllUsesWith(L0InductionVariable);
		L1InductionVariable->eraseFromParent();
	}


	void updateLoopTerminator(Loop *L0, Loop *L1){
		// exit block unico di L1
		BasicBlock *L1ExitBlock = L1->getExitBlock();
		// exiting block di L0
		BasicBlock *L0ExitingBlock = L0->getExitingBlock();

		// i loop sono guarded? --> si modifica il ramo false della guardia di L0 per saltare alla exit di L1
		if(L0->isGuarded() && L1->isGuarded())
			setTerminator(L0->getLoopGuardBranch()->getParent(), L1ExitBlock, 1);

		// si modifica il terminator di L0 facendolo puntare all'uscita di L1
        setTerminator(L0ExitingBlock, L1ExitBlock, 1);
	}


	void executeFusion(Loop *L0, Loop *L1){
		updateLoopTerminator(L0, L1);
		updateInductionVariables(L0, L1);
		linkLoopPair(L0, L1);

		outs() << "\nLoop fusi correttamente:\n"
            << "\tLoop 1: " << L0 << "\n"
            << "\tLoop 2: " << L1 << "\n";
	}


	PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM){
        LoopInfo &LI = AM.getResult<LoopAnalysis>(F);
        DominatorTree &DT = AM.getResult<DominatorTreeAnalysis>(F);
        PostDominatorTree &PDT = AM.getResult<PostDominatorTreeAnalysis>(F);
		ScalarEvolution &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
		DependenceInfo &DI = AM.getResult<DependenceAnalysis>(F);

    	// vettore di top-level loop
		std::vector<Loop*> loops;
		for(auto &L: LI)
			loops.push_back(L);

    	// ci sono meno di 2 loop? --> impossibile fondere
		if(loops.size() < 2)
			return PreservedAnalyses::all();

    	// ordinamento dei loop in base alla dominanza
    	std::sort(loops.begin(), loops.end(), [&DT, this](Loop *L1, Loop *L2){
			BasicBlock *entry1 = getLogicalEntry(L1);
			BasicBlock *entry2 = getLogicalEntry(L2);
			return DT.properlyDominates(entry1, entry2);
		});

    	// coppie di loop candidate alla fusione
		std::vector<std::pair<Loop*, Loop*>> loopPairCandidates = getLoopPairCandidates(loops, DT, PDT, SE, DI);

    	// loop fusion in ordine dei candidati trovati
		for(size_t i=0; i<loopPairCandidates.size(); ++i)
			executeFusion(loopPairCandidates[i].first, loopPairCandidates[i].second);

		/* se sono stati fusi dei loop, si segnala a LLVM di distruggere
			tutti i blocchi rimasti orfani e/o scollegati */
        if(!loopPairCandidates.empty())
            EliminateUnreachableBlocks(F);

		return PreservedAnalyses::none();
	}


	static bool isRequired(){
		return true;
	}
};
}


llvm::PassPluginLibraryInfo getLoopFusionPassPluginInfo(){
	return { LLVM_PLUGIN_API_VERSION, "LoopFusionPass", LLVM_VERSION_STRING, [](PassBuilder &PB){
		PB.registerPipelineParsingCallback(
			[](StringRef Name, FunctionPassManager &FPM, ArrayRef<PassBuilder::PipelineElement>){
				if (Name == "LoopFusionPass"){
					FPM.addPass(LoopFusionPass());
					return true;
				}
				return false;
			}
		);
	}};
}


extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo(){
	return getLoopFusionPassPluginInfo();
}