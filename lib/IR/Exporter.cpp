#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include <map>

using namespace llvm;

namespace Wrapper {
  // Not thread safe
  class LLVMWrapper {
    public:
      LLVMWrapper() {}
      
    private:
      LLVMContext context; // Everyone uses this context
  };

  static LLVMWrapper* TheLLVMWrapper = nullptr;

  static void createLLVMWrapper() {
    TheLLVMWrapper = new LLVMWrapper();
  }
  
  static void disposeLLVMWrapper() {
    delete TheLLVMWrapper;
    TheLLVMWrapper = nullptr;
  }

}

extern "C" {
  void makeLLVMContext() { Wrapper::createLLVMWrapper(); }
  void destroyLLVMContext() { Wrapper::disposeLLVMWrapper(); }
}
