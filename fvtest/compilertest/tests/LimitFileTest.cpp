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
 *******************************************************************************/

#include "tests/LimitFileTest.hpp"

#include <fstream>
#include <string>
#include <sstream>
#include <unistd.h>
#include <sys/stat.h>

#include "tests/OMRTestEnv.hpp"
#include "tests/OpCodesTest.hpp"

TestCompiler::LimitFileTest::LimitFileTest()
   {
   // Don't use fork(), since that doesn't let us initialize the JIT
   ::testing::FLAGS_gtest_death_test_style = "threadsafe";
   }

TestCompiler::LimitFileTest::~LimitFileTest()
   {
   // Remove all generated vlog files.
   for(auto it = _vlog.begin(); it != _vlog.end(); ++it)
      {
      unlink(*it);
      }
   }

void
TestCompiler::LimitFileTest::compileTests()
   {
   ::TestCompiler::OpCodesTest unaryTest;
   unaryTest.compileUnaryTestMethods();
   //unaryTest.invokeUnaryTests();
   }

/**
 * Determine if a file exists.
 *
 * @param name The name of the file.
 */
bool
TestCompiler::LimitFileTest::fileExists(const char *name)
   {
   // TODO: use port library for file operations
   struct stat buf;
   int result = stat(name, &buf);
   return result == 0;
   }

/**
 * Startup the JIT, run tests, shut down the JIT, then exit with
 * an exit code of 0.
 * This must be called in a process that has not initialized the
 * JIT yet.
 *
 * @param vlog The file to log to.
 * @param limitFile A limit file argument.
 */
void
TestCompiler::LimitFileTest::generateVLog(const char *vlog, const char *limitFile)
   {
   std::string args = std::string("-Xjit:verbose,disableSuffixLogs,vlog=");
   args = args + vlog;

   // Add the limit file, if provided.
   if(limitFile)
      args = args + ",limitFile=" + limitFile;

   OMRTestEnv::initialize(const_cast<char *>(args.c_str()));
   compileTests();
   OMRTestEnv::shutdown();

   exit(0);
   }

/**
 * Assert if a method was or was not compiled in a test, by searching
 * the vlog file for the method.
 *
 * @param[in] vlog The log from the test.
 * @param[in] method The name of the method to search for.
 * @param[in] compiled Assert that the method was or was not compiled.
 * @param[out] The line of the log that the method is on. This can be
 *             used in a limit file parameter. This parameter is only
 *             valid when compiled is true.
 */
void
TestCompiler::LimitFileTest::checkVLogForMethod(const char *vlog, const char *method, bool compiled, int *foundOnLine)
   {
   std::ifstream vlogFile(vlog);
   ASSERT_TRUE(vlogFile.is_open());

   bool foundPlus = false;
   bool foundSpace = false;
   std::string line;
   for(int i = 1; std::getline(vlogFile, line); ++i)
      {
      // If method hasn't been compiled, it shouldn't be anywhere in the vlog
      if(!compiled)
         {
         ASSERT_EQ(line.find(method), std::string::npos);
         continue;
         }

      if(line.empty())
         continue;

      // Lines in a vlog file are dependent on their first character.
      switch(line[0])
         {
         // Method successfully compiled
         case '+':
            if(line.find(method) != std::string::npos)
               {
               ASSERT_NE(line.find("(warm"), std::string::npos);
               foundPlus = true;
               if(compiled && foundOnLine)
                  *foundOnLine = i;
               }
            break;
         // Method compilation began
         case ' ':
            if(line.find(method) != std::string::npos)
               {
               ASSERT_NE(line.find(" compiling"), std::string::npos);
               foundSpace = true;
               }
            break;
         }
      }

   if(!compiled)
      return;

   // If asserting compilation, we should have found both a compilation
   // beginning message, and a successful compilation message.
   ASSERT_TRUE(foundPlus);
   ASSERT_TRUE(foundSpace);
   }

/**
 * Create a vlog file by creating a new process and running a set of tests.
 *
 * In order to properly test the vlog and limit file arguments, a JIT must
 * be initialized. Since a JIT is already initialized when these tests are
 * run, and since shutting down the JIT does not reset everything, we need
 * to create a new process that initializes a JIT with its own options.
 *
 * Note that this is made possible by disabling the global test environment
 * in main.cpp for any new processes in this file.
 *
 * @param vlog The location to place the vlog file.
 * @param limitFile A limit file argument. This can be of the form
 *        "limitfile", "(limitfile,n), or (limitfile,n,m).
 */
void
TestCompiler::LimitFileTest::createVLog(const char *vlog, const char *limitFile)
   {
   _vlog.push_back(vlog);

   /* This creates the new process, runs generateVLog, and asserts it exits
    * with a status code of 0.
    */
   ASSERT_EXIT(generateVLog(vlog, limitFile), ::testing::ExitedWithCode(0), "");

   ASSERT_TRUE(fileExists(vlog));
   }

/**
 * Create a vlog file by creating a new process and running a set of tests,
 * then asserting that select methods were compiled. The line number of
 * a compiled method is placed in methodLine.
 *
 * @param[in] vlog The location to place the vlog file.
 * @param[in] limitFile A limit file argument.
 * @param[out] methodLine A pointer to the line of the vlog where a method
 *             was compiled. This is currently the method `iNeg`, however
 *             this is subject to change.
 */
void
TestCompiler::LimitFileTest::createAndCheckVLog(const char *vlog, const char *limitFile, int *methodLine)
   {
   createVLog(vlog, limitFile);

   checkVLogForMethod(vlog, "iNeg", true, methodLine);
   checkVLogForMethod(vlog, "iReturn", true);
   }

namespace TestCompiler {

// Assert a vlog file was created.
TEST_F(LimitFileTest, CreateVLogTest)
   {
   const char *vlog = "createVLog.log";
   createVLog(vlog);
   }

// Assert that methods were compiled and logged.
TEST_F(LimitFileTest, CheckVLogTest)
   {
   const char *vlog = "checkVLog.log";
   createAndCheckVLog(vlog);
   }

// Use a limit file.
TEST_F(LimitFileTest, UseLimitFileTest)
   {
   const char *vlog = "checkLimitFileSuccess.log";
   const char *limitFile = "checkLimitFileSuccess.limit";

   // Create limit file.
   createAndCheckVLog(limitFile, NULL);

   createAndCheckVLog(vlog, limitFile);
   }

// Use (limitfile,n,m) notation.
TEST_F(LimitFileTest, UseLimitFileRangeTest)
   {
   const char *vlog = "checkLimitFileSuccess.log";
   const char *limitFile = "checkLimitFileSuccess.limit";

   // Create limit file.
   int iNegLine;
   createAndCheckVLog(limitFile, NULL, &iNegLine);

   // Note VC++ 2010 doesn't support std::to_string(int).
   std::string iNegLineStr = std::to_string(static_cast<long long>(iNegLine));
   std::string limitArg = std::string("(") + limitFile + "," + iNegLineStr + "," + iNegLineStr + ")";
   createVLog(vlog, limitArg.c_str());

   checkVLogForMethod(vlog, "iNeg", true);
   checkVLogForMethod(vlog, "iReturn", false);
   }

// Use (limitfile,n) notation.
TEST_F(LimitFileTest, UseLimitFileBoundTest)
   {
   const char *vlog = "checkLimitFileSuccess.log";
   const char *limitFile = "checkLimitFileSuccess.limit";

   // Create limit file.
   int iNegLine;
   createAndCheckVLog(limitFile, NULL, &iNegLine);

   iNegLine++; // Start at line after iNeg.
   std::string limitArg = std::string("(") + limitFile + "," + std::to_string(static_cast<long long>(iNegLine)) + ")";
   createVLog(vlog, limitArg.c_str());
   checkVLogForMethod(vlog, "iNeg", false);
   }

}
