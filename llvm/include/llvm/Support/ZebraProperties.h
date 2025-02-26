#ifndef LLVM_ZEBRAPROPERTIES_H
#define LLVM_ZEBRAPROPERTIES_H

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
class ZebraProperties {

public:

  /// The different states of an annotated function.
  enum State {
    /// Marked as needing instrumentation, but not yet visited by pass.
    Marked = 0,

    /// Original function that is kept as-is.
    Original = 1,

    /// Copied function that is instrumented to interleave data with counters.
    Copy = 2,

    /// Copied function where interleaving already happened
    CopyRewritten = 3,

    /// Zebra-protected function residing in another compilation unit or library.
    /// Necessary to correctly transform calls to instrumented functions.
    Extern = 4,

    /// Automatically copied version of a function that was called from another
    /// instrumented function.
    AutoCopy = 5
  };

private:

  uint32_t Data = 0;

  static constexpr uint32_t StateBits = 3; // Hint: also adjust this when adding further state bits!!
  static constexpr uint32_t StateMask = (1 << StateBits) - 1;

  ZebraProperties(uint32_t Data) : Data(Data) {}

  void setState(State state) {
    Data &= ~StateMask;
    Data |= static_cast<uint32_t>(state);
  }

  friend raw_ostream &operator<<(raw_ostream &OS, ZebraProperties &ZP);

public:

  /// Creates an ZebraProperties annotation with the given state.
  explicit ZebraProperties(State State) {
    setState(State);
  }

  /// Create an ZebraProperties object from an encoded integer value (used by the IR
  /// attribute).
  static ZebraProperties createFromIntValue(uint32_t Data) {
    return ZebraProperties(Data);
  }

  /// Convert this object into an encoded integer value (used by the IR
  /// attribute).
  uint32_t toIntValue() const {
    return Data;
  }

  /// Returns the state.
  State getState() const {
    return static_cast<State>(Data & StateMask);
  }

  /// Get a copy of this object with modified state.
  ZebraProperties getWithState(State NewState) const {
    ZebraProperties ZP = *this;
    ZP.setState(NewState);
    return ZP;
  }

  static const char *getStateString(State State) {
    switch(State) {
    case State::Marked:
      return "marked";
    case State::Original:
      return "original";
    case State::Copy:
      return "copy";
    case State::CopyRewritten:
      return "copy-rewritten";
    case State::Extern:
      return "extern";
    case State::AutoCopy:
      return "autocopy";
    }
    llvm_unreachable("Invalid ZebraProperties State");
  }

};

/// Debug print ZebraProperties.
raw_ostream &operator<<(raw_ostream &OS, const ZebraProperties &ZP);

} // namespace llvm

#endif // LLVM_ZEBRAPROPERTIES_H
