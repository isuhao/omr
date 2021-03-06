/*******************************************************************************
 *
 * (c) Copyright IBM Corp. 2000, 2016
 *
 *  This program and the accompanying materials are made available
 *  under the terms of the Eclipse Public License v1.0 and
 *  Apache License v2.0 which accompanies this distribution.
 *
 *      The Eclipse Public License is available at
 *      http://www.eclipse.org/legal/epl-v10.html
 *
 *      The Apache License v2.0 is available at
 *      http://www.opensource.org/licenses/apache2.0.php
 *
 * Contributors:
 *    Multiple authors (IBM Corp.) - initial implementation and documentation
 *******************************************************************************/

#include "optimizer/Dominators.hpp"

#include <stddef.h>                            // for NULL
#include <stdint.h>                            // for int32_t
#include "codegen/FrontEnd.hpp"
#include "compile/Compilation.hpp"             // for Compilation, etc
#include "control/Options.hpp"
#include "control/Options_inlines.hpp"
#include "cs2/arrayof.h"                       // for StaticArrayOf, etc
#include "cs2/bitvectr.h"                      // for ABitVector<>::BitRef
#include "cs2/sparsrbit.h"
#include "cs2/tableof.h"                       // for TableOf
#include "env/IO.hpp"
#include "env/TRMemory.hpp"                    // for Allocator
#include "il/Block.hpp"                        // for Block, toBlock
#include "il/symbol/ResolvedMethodSymbol.hpp"  // for ResolvedMethodSymbol
#include "infra/Assert.hpp"                    // for TR_ASSERT
#include "infra/Cfg.hpp"                       // for CFG, etc
#include "infra/List.hpp"                      // for ListElement, List
#include "infra/TRCfgEdge.hpp"                 // for CFGEdge
#include "infra/TRCfgNode.hpp"                 // for CFGNode
#include "ras/Debug.hpp"                       // for TR_DebugBase

TR_Dominators::TR_Dominators(TR::Compilation *c, bool post)
   : _compilation(c),
   	 _info(c->getFlowGraph()->getNextNodeNumber()+1, c->allocator(), c->allocator()),
   	 _dfNumbers(c->getFlowGraph()->getNextNodeNumber()+1, c->allocator(), 0),
   	 _dominators(c->getFlowGraph()->getNextNodeNumber()+1, c->allocator(), NULL)
   {
   LexicalTimer tlex("TR_Dominators::TR_Dominators", _compilation->phaseTimer());

   _postDominators = post;
   _isValid = true;
   _topDfNum = 0;
   _visitCount = c->incOrResetVisitCount();
   _trace = comp()->getOption(TR_TraceDominators);

   TR::Block *block;
   TR::CFG *cfg = c->getFlowGraph();

   _cfg = c->getFlowGraph();
   _numNodes = cfg->getNumberOfNodes()+1;

   if (trace())
      {
      traceMsg(comp(), "Starting %sdominator calculation\n", _postDominators ? "post-" : "");
      traceMsg(comp(), "   Number of nodes is %d\n", _numNodes-1);
      }

   if (_postDominators)
      _dfNumbers[cfg->getStart()->getNumber()] = -1;
   else
      _dfNumbers[cfg->getEnd()->getNumber()] = -1;

   findDominators(toBlock( _postDominators ? cfg->getEnd() : cfg->getStart() ));

   int32_t i;
   for (i = _topDfNum; i > 1; i--)
      {
      BBInfo &info = getInfo(i);
      TR::Block *dominated = info._block;
      TR::Block *dominator = getInfo(info._idom)._block;
      _dominators[dominated->getNumber()] = dominator;
      if (trace())
         traceMsg(comp(), "   %sDominator of block_%d is block_%d\n", _postDominators ? "post-" : "",
                                      dominated->getNumber(), dominator->getNumber());
      }

   // The exit block may not be reachable from the entry node. In this case just
   // give the exit block the highest depth-first numbering.
   // No other blocks should be unreachable.
   //

   if (_postDominators)
      {
      if (_dfNumbers.ValueAt(cfg->getStart()->getNumber()) < 0)
         _dfNumbers[cfg->getStart()->getNumber()] = _topDfNum++;
      }
   else
      {
      if (_dfNumbers.ValueAt(cfg->getEnd()->getNumber()) < 0)
         _dfNumbers[cfg->getEnd()->getNumber()] = _topDfNum++;
      }

   // Assert that we've found every node in the cfg.
   //
   if (_topDfNum != _numNodes-1)
      {
      if (_postDominators)
         {
         _isValid = false;
         if (trace())
            traceMsg(comp(), "Some blocks are not reachable from exit. Post-dominator info is invalid.\n");
         return;
         }
      else
         TR_ASSERT(false, "Unreachable block in the CFG %d %d", _topDfNum, _numNodes-1);
      }

   #if DEBUG
      for (block = toBlock(cfg->getFirstNode()); block; block = toBlock(block->getNext()))
         {
         TR_ASSERT(_dfNumbers.ValueAt(block->getNumber()) >= 0, "Unreachable block in the CFG");
         }
   #endif

   if (trace())
      traceMsg(comp(), "End of %sdominator calculation\n", _postDominators ? "post-" : "");

   // Release no-longer-used CS2 data
   _info.ShrinkTo(0);
   }

// The following constructor needs to be shortened by finding common code with the
// constructor above
TR_Dominators::TR_Dominators(TR::Compilation *c, TR::ResolvedMethodSymbol* methSym, bool post)
   : _compilation(c),
     _cfg(methSym->getFlowGraph()),
   	 _info(methSym->getFlowGraph()->getNextNodeNumber()+1, c->allocator(), c->allocator()),
   	 _dfNumbers(methSym->getFlowGraph()->getNextNodeNumber()+1, c->allocator(), 0),
   	 _dominators(methSym->getFlowGraph()->getNextNodeNumber()+1, c->allocator(), NULL)
   {
   LexicalTimer tlex("TR_Dominators::TR_Dominators", _compilation->phaseTimer());

   _postDominators = post;
   _isValid = true;
   _topDfNum = 0;
   _visitCount = c->incOrResetVisitCount();
   _trace = comp()->getOption(TR_TraceDominators);

   TR::Block *block;
   TR::CFG *cfg = methSym->getFlowGraph();

   _cfg = methSym->getFlowGraph();
   _numNodes = cfg->getNumberOfNodes()+1;

   if (trace())
      {
      traceMsg(comp(), "Starting %sdominator calculation\n", _postDominators ? "post-" : "");
      traceMsg(comp(), "   Number of nodes is %d\n", _numNodes-1);
      }

   if (_postDominators)
      _dfNumbers[cfg->getStart()->getNumber()] = -1;
   else
      _dfNumbers[cfg->getEnd()->getNumber()] = -1;

   findDominators(toBlock( _postDominators ? cfg->getEnd() : cfg->getStart() ));

   int32_t i;
   for (i = _topDfNum; i > 1; i--)
      {
      BBInfo &info = getInfo(i);
      TR::Block *dominated = info._block;
      TR::Block *dominator = getInfo(info._idom)._block;
      _dominators[dominated->getNumber()] = dominator;
      if (trace())
         traceMsg(comp(), "   %sDominator of block_%d is block_%d\n", _postDominators ? "post-" : "",
                                      dominated->getNumber(), dominator->getNumber());
      }

   // The exit block may not be reachable from the entry node. In this case just
   // give the exit block the highest depth-first numbering.
   // No other blocks should be unreachable.
   //

   if (_postDominators)
      {
      if (_dfNumbers.ValueAt(cfg->getStart()->getNumber()) < 0)
         _dfNumbers[cfg->getStart()->getNumber()] = _topDfNum++;
      }
   else
      {
      if (_dfNumbers.ValueAt(cfg->getEnd()->getNumber()) < 0)
         _dfNumbers[cfg->getEnd()->getNumber()] = _topDfNum++;
      }

   // Assert that we've found every node in the cfg.
   //
   if (_topDfNum != _numNodes-1)
      {
      if (_postDominators)
         {
         _isValid = false;
         if (trace())
            traceMsg(comp(), "Some blocks are not reachable from exit. Post-dominator info is invalid.\n");
         return;
         }
      else
         TR_ASSERT(false, "Unreachable block in the CFG %d %d", _topDfNum, _numNodes-1);
      }

   #if DEBUG
      for (block = toBlock(cfg->getFirstNode()); block; block = toBlock(block->getNext()))
         {
         TR_ASSERT(_dfNumbers.ValueAt(block->getNumber()) >= 0, "Unreachable block in the CFG");
         }
   #endif

   if (trace())
      traceMsg(comp(), "End of %sdominator calculation\n", _postDominators ? "post-" : "");

   // Release no-longer-used CS2 data
   _info.ShrinkTo(0);
   }


TR::Block * TR_Dominators::getDominator(TR::Block *block)
   {
   return _dominators.ValueAt(block->getNumber());
   }

int TR_Dominators::dominates(TR::Block *block, TR::Block *other)
   {

   if (other == block)
      return 1;
   for (TR::Block *d = other; d != NULL && _dfNumbers.ValueAt(d->getNumber()) >= _dfNumbers.ValueAt(block->getNumber()); d = getDominator(d))
      {
      if (d == block)
         return 1;
      }
   return 0;
   }

void TR_Dominators::findDominators(TR::Block *start)
   {
   int32_t i;

   // Initialize the BBInfo structure for the first (dummy) entry
   //
   getInfo(0)._ancestor = 0;
   getInfo(0)._label = 0;

   if (trace())
      {
      traceMsg(comp(), "CFG before initialization:\n");
      comp()->getDebug()->print(comp()->getOutFile(), _cfg);
      }

   // Initialize the BBInfo structures for the real blocks
   //
   initialize(start, NULL);

   if (trace())
      {
	  traceMsg(comp(), "CFG after initialization:\n");
      comp()->getDebug()->print(comp()->getOutFile(), _cfg);
      traceMsg(comp(), "\nInfo after initialization:\n");
      for (i = 0; i <= _topDfNum; i++)
         getInfo(i).print(comp()->fe(), comp()->getOutFile());
      }

   for (i = _topDfNum; i > 1; i--)
      {
      BBInfo &w = getInfo(i);

      int32_t u;

      // Compute initial values for semidominators and store blocks with the
      // same semidominator in the semidominator's bucket.
      //
      TR::CFGEdge *pred, *succ;

      if (_postDominators)
         {
         TR_SuccessorIterator bi(w._block);
         for (succ = bi.getFirst(); succ != NULL; succ = bi.getNext())
            {
            u = eval(_dfNumbers.ValueAt(toBlock(succ->getTo())->getNumber())+1);
            u = getInfo(u)._sdno;
            if (u < w._sdno)
               w._sdno = u;
            }
         }
      else
         {
         TR_PredecessorIterator bi(w._block);
         for (pred = bi.getFirst(); pred != NULL; pred = bi.getNext())
            {
            u = eval(_dfNumbers.ValueAt(toBlock(pred->getFrom())->getNumber())+1);
            u = getInfo(u)._sdno;
            if (u < w._sdno)
               w._sdno = u;
            }
         }

      BBInfo &v = getInfo(w._sdno);
      v._bucket[i] = true;
      link(w._parent, i);

      // Compute immediate dominators for nodes in the bucket of w's parent
      //
      SparseBitVector &parentBucket = getInfo(w._parent)._bucket;
      SparseBitVector::Cursor cursor(parentBucket);
      for (cursor.SetToFirstOne(); cursor.Valid(); cursor.SetToNextOne())
         {
         BBInfo &sdom = getInfo(cursor);
         u = eval(cursor);
         if (getInfo(u)._sdno < sdom._sdno)
        	 sdom._idom = u;
         else
        	 sdom._idom = w._parent;
         }
      parentBucket.Clear();
      }

   // Adjust immediate dominators of nodes whose current version of the
   // immediate dominator differs from the node's semidominator.
   //
   for (i = 2; i <= _topDfNum; i++)
      {
      BBInfo &w = getInfo(i);
      if (w._idom != w._sdno)
         w._idom = getInfo(w._idom)._idom;
      }
   }

void TR_Dominators::initialize(TR::Block *start, BBInfo *nullParent) {

   // Set up to start at the start block
   //
   TR::CFGEdge dummyEdge;
   TR::list<TR::CFGEdge*> dummyList(getTypedAllocator<TR::CFGEdge*>(comp()->allocator()));
   dummyList.push_front(&dummyEdge);
   CS2::ArrayOf<StackInfo, TR::Allocator> stack(_numNodes/2, comp()->allocator());
   stack[0].curIterator = dummyList.begin();
   stack[0].list = &dummyList;

   stack[0].parent = -1;
   int32_t stackTop = 1;
   while (stackTop>0) {
      auto next = stack[--stackTop].curIterator;
      auto list = stack[stackTop].list;
      TR::CFGNode* succ = _postDominators ? (*next)->getFrom() : (*next)->getTo();
      TR::Block *block;
      if (succ)
         block = toBlock(succ);
      else
         block = start;

      TR::Block *nextBlock;
      if (block->getVisitCount() == _visitCount)
         {
         // This block has already been processed. Just set up the next block
         // at this level.
         //
         ++next;
         if (next != list->end())
            {
            if (trace())
               {
               if (_postDominators)
                  traceMsg(comp(), "Insert block_%d at level %d\n", toBlock((*next)->getFrom())->getNumber(), stackTop);
               else
                  traceMsg(comp(), "Insert block_%d at level %d\n", toBlock((*next)->getTo())->getNumber(), stackTop);

               }
            stack[stackTop++].curIterator = next;
            }
         continue;
         }

      if (trace())
         traceMsg(comp(), "At level %d block_%d becomes block_%d\n", stackTop, block->getNumber(), _topDfNum);

      block->setVisitCount(_visitCount);
      _dfNumbers[block->getNumber()] = _topDfNum++;

      // Note: the BBInfo index is one more than the block's DF number.
      //
      BBInfo &binfo = getInfo(_topDfNum);
      binfo._block = block;
      binfo._sdno = _topDfNum;
      binfo._label = _topDfNum;
      binfo._ancestor = 0;
      binfo._child = 0;
      binfo._size = 1;
      binfo._parent = stack[stackTop].parent;

      // Set up the next block at this level
      //
      ++next;
      if (next != list->end())
         {
         if (trace())
            {
            if (_postDominators)
               traceMsg(comp(), "Insert block_%d at level %d\n", toBlock((*next)->getFrom())->getNumber(), stackTop);
            else
               traceMsg(comp(), "Insert block_%d at level %d\n", toBlock((*next)->getTo())->getNumber(), stackTop);
            }

         stack[stackTop++].curIterator = next;
         }

      // Set up the successors to be processed
      //
      if (_postDominators)
         list = &(block->getExceptionPredecessors());
      else
         list = &(block->getExceptionSuccessors());

      next = list->begin();
      if (next != list->end())
         {
         if (trace())
            {
            if (_postDominators)
               traceMsg(comp(), "Insert block_%d at level %d\n", toBlock((*next)->getFrom())->getNumber(), stackTop);
            else
               traceMsg(comp(), "Insert block_%d at level %d\n", toBlock((*next)->getTo())->getNumber(), stackTop);
            }

         stack[stackTop].curIterator = next;
         stack[stackTop].list = list;
         stack[stackTop].parent = _topDfNum;
         stackTop++;
         }

      if (_postDominators)
         list = &(block->getPredecessors());
      else
         list = &(block->getSuccessors());

      next = list->begin();
      if (next != list->end())
         {
         if (trace())
            {
            if (_postDominators)
               traceMsg(comp(), "Insert block_%d at level %d\n", toBlock((*next)->getFrom())->getNumber(), stackTop);
            else
               traceMsg(comp(), "Insert block_%d at level %d\n", toBlock((*next)->getTo())->getNumber(), stackTop);
            }
         stack[stackTop].list = list;
         stack[stackTop].curIterator = next;
         stack[stackTop].parent = _topDfNum;
         stackTop++;
         }
      }
   }

// Determine the ancestor of the block at the given index whose semidominator has the minimal depth-first
// number.
//
int32_t TR_Dominators::eval(int32_t index)
   {
   	BBInfo &bbInfo = getInfo(index);
   if (bbInfo._ancestor == 0)
      return bbInfo._label;
   compress(index);
   BBInfo &ancestor = getInfo(bbInfo._ancestor);
   if (getInfo(ancestor._label)._sdno >= getInfo(bbInfo._label)._sdno)
      return bbInfo._label;
   return ancestor._label;
   }

// Compress ancestor path of the block at the given index to the block whose label has the maximal
// semidominator number.
//
void TR_Dominators::compress(int32_t index)
   {
   	BBInfo & bbInfo = getInfo(index);
   	BBInfo &ancestor = getInfo(bbInfo._ancestor);
   if (ancestor._ancestor != 0)
      {
      compress(bbInfo._ancestor);
      if (getInfo(ancestor._label)._sdno < getInfo(bbInfo._label)._sdno)
         bbInfo._label = ancestor._label;
      bbInfo._ancestor = ancestor._ancestor;
      }
   }

// Rebalance the forest of trees maintained by the ancestor and child links
//
void TR_Dominators::link(int32_t parentIndex, int32_t childIndex)
   {
   BBInfo &binfo = getInfo(childIndex);
   int32_t sdno = getInfo(binfo._label)._sdno;
   int32_t si = childIndex;
   BBInfo *s = &binfo;
   while (sdno < getInfo(getInfo(s->_child)._label)._sdno)
      {
      BBInfo &child = getInfo(s->_child);
      if (s->_size + getInfo(child._child)._size >= 2*child._size)
         {
         child._ancestor = si;
         s->_child = child._child;
         }
      else
         {
         child._size = s->_size;
         s->_ancestor = s->_child;
         si = s->_child;
         s = &getInfo(si);
         }
      }
   s->_label = binfo._label;
   BBInfo & parent = getInfo(parentIndex);
   parent._size += binfo._size;
   if (parent._size < 2*binfo._size)
      {
      int32_t tmp = si;
      si = parent._child;
      parent._child = tmp;
      }
   while (si != 0)
      {
      s = &getInfo(si);
      s->_ancestor = parentIndex;
      si = s->_child;
      }
   }

#ifdef DEBUG
void TR_Dominators::BBInfo::print(TR_FrontEnd *fe, TR::FILE *pOutFile)
   {
   if (pOutFile == NULL)
      return;
   trfprintf(pOutFile,"BBInfo %d:\n",getIndex());
   trfprintf(pOutFile,"   _parent=%d, _idom=%d, _sdno=%d, _ancestor=%d, _label=%d, _size=%d, _child=%d\n",
             _parent,
             _idom,
             _sdno,
             _ancestor,
             _label,
             _size,
             _child);
   }
#endif


void TR_PostDominators::findControlDependents()
   {
   int32_t nextNodeNumber = _cfg->getNextNodeNumber();
   int32_t i;

   // Initialize the table of direct control dependents
   for (i = 0; i < nextNodeNumber; i++)
      _directControlDependents.AddEntry(_directControlDependents);

   TR::Block * block;
   for (block = comp()->getStartBlock(); block!=NULL; block = block->getNextBlock())
      {
      SparseBitVector &bv = _directControlDependents[block->getNumber()+1];

      auto next = block->getSuccessors().begin();
      while (next != block->getSuccessors().end())
         {
         TR::Block *p;
         p = toBlock((*next)->getTo());
         while (p != getDominator(block))
	        {
            bv[p->getNumber()] = true;
            p = getDominator(p);
            }
         ++next;
         }
      }

   if (trace())
      {
      for (i = 0; i < nextNodeNumber; i++)
         {
         SparseBitVector::Cursor cursor(_directControlDependents[i+1]);
         cursor.SetToFirstOne();
         traceMsg(comp(), "Block %d controls blocks: {", i);
         if (cursor.Valid())
         	  {
         	  int32_t b = cursor;
         	  traceMsg(comp(),"%d", b);
         	  while(cursor.SetToNextOne())
         	     {
         	     b = cursor;
         	     traceMsg(comp(),", %d", b);
         	     }
         	  }
         traceMsg(comp(), "} \t\t%d blocks in total\n", numberOfBlocksControlled(i));
         }
      }
   }


int32_t TR_PostDominators::numberOfBlocksControlled(int32_t block)
   {
   BitVector seen(comp()->allocator());
   return countBlocksControlled(block, seen);
   }

int32_t TR_PostDominators::countBlocksControlled(int32_t block, BitVector &seen)
   {
   int32_t number = 0;
   SparseBitVector::Cursor cursor(_directControlDependents[block+1]);
   for (cursor.SetToFirstOne(); cursor.Valid(); cursor.SetToNextOne())
      {
      int32_t i = cursor;
      if (!seen[i])
         {
         number++;
         seen[i] = true;
         number += countBlocksControlled(i, seen);
         }
      }
   return number;
   }
