#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ids.hpp"
#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "simTime.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimReplay {

// Why a pass went to an opponent (docs/decision-trace.md):
enum class FailureCause : std::uint8_t {
  // The passer did not know the interceptor was there: he had not seen him,
  // or believed him far from where he was.
  kPerception,
  // The passer saw the danger and chose the pass anyway: its estimated
  // interception risk was high.
  kDecision,
  // The passer saw the interceptor and judged the pass safe; the kick
  // itself -- execution error, pressure on the passer -- let it fail.
  kExecution,
};

[[nodiscard]] std::string_view failureCauseName(FailureCause cause) noexcept;

// What became of a traced pass decision.
enum class PassResult : std::uint8_t {
  // Still under way when the trace ended.
  kPending,
  // The passer lost the ball before he could play it.
  kNotPlayed,
  kReceived,
  kIntercepted,
  // Nobody took the ball but the passer himself.
  kRecoveredByPasser,
};

[[nodiscard]] std::string_view passResultName(PassResult result) noexcept;

struct PassOutcome {
  PassResult result = PassResult::kPending;
  std::optional<SimCore::SimTick> tick;
  // The receiver or interceptor.
  std::optional<SimCore::PlayerId> by;
  // For an interception only.
  std::optional<FailureCause> cause;
  // For a perception failure: how far from the interceptor's true position
  // the passer believed him, empty if he had not seen him at all.
  std::optional<double> believedMetersOff;

  friend bool operator==(const PassOutcome&, const PassOutcome&) = default;
};

// A decision of a player on the ball and, if he passed, what became of it.
struct PassDecisionTrace {
  SimMatch::DecisionDiagnostic decision;
  PassOutcome outcome;

  friend bool operator==(const PassDecisionTrace&, const PassDecisionTrace&) = default;
};

// A decision of a player without the ball.
struct ActionDecisionTrace {
  SimMatch::ActionDiagnostic decision;

  friend bool operator==(const ActionDecisionTrace&, const ActionDecisionTrace&) = default;
};

using TraceEntry = std::variant<PassDecisionTrace, ActionDecisionTrace>;

// The limits of outcome attribution.
struct TraceConfig {
  // A passer who believed the interceptor farther than this from where he
  // was misjudged him: a perception failure.
  double perceptionErrorMeters = 3.0;
  // A chosen pass with at least this estimated interception risk was a
  // risky decision.
  double riskyDecision = 0.5;

  friend bool operator==(const TraceConfig&, const TraceConfig&) = default;
};

// Where the players really were when a decision was taken.
using TruePositions = std::map<SimCore::PlayerId, SimCore::Vec2>;

// Why the pass the decision chose went to the interceptor: perception if the
// passer had not seen him (no observation of at least the scoring's
// minConfidence) or believed him more than perceptionErrorMeters from his
// true position, decision if the pass's estimated interception risk was at
// least riskyDecision, execution otherwise.
[[nodiscard]] PassOutcome attributeInterception(const SimMatch::PassIntercepted& event,
                                                const SimMatch::DecisionDiagnostic& decision,
                                                const TruePositions& truePositions,
                                                const SimMatch::MatchConfig& matchConfig,
                                                const TraceConfig& config);

// Collects the decisions a simulation explains -- turn diagnostics on, and
// filter them, before stepping -- and follows every pass decision to its
// outcome. Reads diagnostics, events and the state the decisions were taken
// in; changes nothing.
class DecisionTracer {
 public:
  explicit DecisionTracer(const SimMatch::MatchConfig& config, TraceConfig traceConfig = {});

  // After every step of the simulation.
  void recordStep(const SimMatch::MatchSimulation& simulation);

  // Every decision so far, in the order they were made.
  [[nodiscard]] std::span<const TraceEntry> entries() const noexcept { return entries_; }

 private:
  void resolve(const SimMatch::MatchEvent& event);

  SimMatch::MatchConfig matchConfig_;
  TraceConfig config_;
  std::vector<TraceEntry> entries_;
  // The pass decision waiting for its outcome: index into entries_.
  std::optional<std::size_t> openPass_;
  bool openPassPlayed_ = false;
  // Where everyone was when the open pass was decided.
  TruePositions openPassPositions_;
};

// The trace as text, one line per decision, for reading in a terminal.
[[nodiscard]] std::string formatTrace(std::span<const TraceEntry> entries);

}  // namespace ElyverseFootball::SimReplay
