/*******************************************************************************
 *
 * (c) Copyright IBM Corp. 2016, 2016
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
 ******************************************************************************/


#ifndef STRUCTARRAY_INCL
#define STRUCTARRAY_INCL

#include "ilgen/MethodBuilder.hpp"

namespace TR { class TypeDictionary; }

typedef void* (CreateStructArrayFunctionType)(int);

typedef void (ReadStructArrayFunctionType)(void*, int);

class CreateStructArrayMethod : public TR::MethodBuilder
   {
   private:

   TR::IlType *StructType;
   TR::IlType *pStructType;

   public:
   CreateStructArrayMethod(TR::TypeDictionary *);
   virtual bool buildIL();
   };

class ReadStructArrayMethod : public TR::MethodBuilder
   {
   private:

   TR::IlType *StructType;
   TR::IlType *pStructType;

   public:
   ReadStructArrayMethod(TR::TypeDictionary *);
   virtual bool buildIL();
   };

#endif // !defined(LOCALARRAY_INCL)
