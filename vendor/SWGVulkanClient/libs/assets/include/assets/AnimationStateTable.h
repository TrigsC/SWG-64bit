#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace assets {

// LAT states contain recursive selector, speed, priority and ambient wrappers.
// SSAT uses literal VAL-space mappings and DFLT; SPAT preserves an idle
// subtree plus moving children. The normal offline runtime evaluates those
// children using authored speeds and a timed idle/moving blend. PUNF stores
// locomotion and transform priority-group names; it is not a selector axis.
enum class AnimationNodeKind {
    // PXAT leaf. Optional PUNF strings name the locomotion and transform
    // priority groups; a single-priority wrapper delegates the pose unchanged.
    Clip,
    // A real named N-way switch (`FORM SSAT` -> `FORM ANMS`).
    // `parameterName` is the switch's own real parameter name (confirmed
    // real values: "gender", "mood" - genuinely just a string, no fixed
    // enum of real names is assumed); `children` holds one real node per
    // real ANMS option, in real file order.
    Switch,
    // Legacy generic container kind; parsed SPAT nodes use Speed below.
    Container,
    // FORM SPAT is SpeedSkeletalAnimationTemplate: runtime speed selects
    // adjacent authored locomotion children, with a timed idle/moving blend.
    Speed,
    // AGAT wraps a continuously advancing LOOP and periodically requests
    // named actions from ACTN. INFO is its min/max timer range; ACTN weights
    // are relative weights in file order, not probabilities or channels.
    Variant,
};

struct AnimationActionTrigger {
    std::string name;
    float weight = 0.0f;  // Raw authored ACTN weight, before normalization.
};

struct AnimationNode {
    AnimationNodeKind kind = AnimationNodeKind::Clip;

    std::string clipPath;           // Clip only
    std::string punfParameterName;  // First PUNF string: locomotion priority group; legacy field name.
    std::string transformPriorityGroupName; // Second PUNF string; not a selector parameter.

    std::string parameterName;  // Switch only

    // SSAT's literal value -> child-index table from CHNK "VAL " (space).
    // Missing/unmatched values use DFLT. Verified against raw asset bytes
    // and the shipped client's selector loader; no filename inference.
    std::vector<std::pair<std::string, int>> switchValueMap;
    // Switch only - the real `CHNK DFLT` child index, -1 if this switch has
    // no real DFLT chunk. The real client falls back to this whenever the
    // runtime value isn't found in switchValueMap (or no value is known at
    // all) - this is the real, authoritative "default" signal, not "first
    // child in file order" (which this project used as an approximation
    // before DFLT was decoded, and which is NOT always correct - see the
    // "mood" switch comment on AnimationSelectionContext).
    int switchDefaultIndex = -1;  // Switch only

    std::vector<std::string> triggerNames;  // Variant only, real ACTN names
    std::vector<AnimationActionTrigger> actionTriggers; // Same order; retains raw weights.
    float minDurationSeconds = 0.0f;        // Variant only
    float maxDurationSeconds = 0.0f;        // Variant only

    // Switch: one real child per ANMS option. Speed: all authored SPAT
    // children. Container: a legacy generic child list. Variant: exactly one (the
    // wrapped clip). Clip: always empty.
    std::vector<AnimationNode> children;
};

// One real named animation state from a `.lat` file - e.g.
// "loop_combat_standing", "trn_prone_to_standing". `root` is the state's
// own real top-level node - for the common case (most real states checked
// so far - simple named transitions, single combat moves) this is
// directly a `Clip` node; for a real multi-variant state it's a `Switch`
// or `Speed` as described above.
struct AnimationState {
    std::string name;
    AnimationNode root;
};

// Plain data from a real ".lat" file (FORM LATT) - a per-skeleton table of
// every named real animation state (bind poses, loops, and transitions),
// resolved via a real skeletal appearance's LATX chunk (see
// SkeletalAppearance.h). Real structure confirmed by dumping an actual
// extracted .lat: FORM(LATT) -> FORM(0000) -> CHNK INFO (own ash filename,
// NUL-terminated, + a trailing uint16 real state count - not otherwise
// consumed) followed by `numStates` real FORM(ANIM) children, each
// CHNK INFO (the state's own name, NUL-terminated, no count prefix) + one
// real node tree as described on AnimationNode above.
struct AnimationStateTableData {
    std::vector<AnimationState> states;
    std::string hierarchyFilename; // Table's own INFO ASH path, if present.
};

// What to select with, given a real state's node tree. `gender` should be
// an authored value "m"/"f"/"o" ("male"/"female" aliases are normalized)
// before lookup in a "gender" Switch's
// own `switchValueMap` (the real mechanism - see AnimationNode's own
// comment), falling back to `switchDefaultIndex` if the value isn't
// present. `mood` is the real server-driven mood value (from
// `CreatureObjectBaseline6::moodString`, see libs/creatureanim) - looked up
// the identical way against a real "mood" Switch. Any `Switch` whose
// `parameterName` is neither "gender" nor "mood" (there is no known third
// real named switch as of this writing) always falls through to
// `switchDefaultIndex` instead of modeling the real trigger-matching
// system, same as an empty/unset `mood` does.
struct AnimationSelectionContext {
    std::string gender;
    std::string mood;
};

class AnimationStateTable {
public:
    // Throws std::runtime_error if the buffer doesn't parse as expected.
    static AnimationStateTableData parse(const std::vector<uint8_t>& bytes);
};

// Resolve only authored SSAT VAL-space/DFLT selections. Retains Speed and
// Variant wrappers and their metadata. No filename inference. Returned pointers
// refer to root's storage and remain valid while that tree is unchanged.
const AnimationNode* resolveAnimationNode(const AnimationNode& root,
                                         const AnimationSelectionContext& context);

// Return all authored SPAT child nodes after their SSAT selections, in file
// order. A selected idle AGAT remains a Variant; the caller classifies speed
// from the child's animation data and can consume its timer/action metadata.
// Throws if the selected root is not Speed or a child selector is invalid.
std::vector<const AnimationNode*> selectSpeedAnimationNodes(const AnimationNode& root,
                                                          const AnimationSelectionContext& context);

// Resolve SSAT and unwrap a single AGAT LOOP to its PXAT leaf. The returned
// Clip retains path and both literal PUNF priority group names. Returns nullptr
// for a Speed/composite or missing/ambiguous child; does not infer action aliases
// or channels from the clip filename.
const AnimationNode* selectAnimationClipNode(const AnimationNode& root,
                                            const AnimationSelectionContext& context);

// Resolve authored string switches and the idle/moving children of a SPAT.
// Returns all speed children; the caller must use their authored speeds and
// evaluate the composite rather than choosing the first moving clip.
std::vector<std::string> selectSpeedAnimationClips(const AnimationNode& root,
                                                 const AnimationSelectionContext& context);

// Walks `root` per `context` and returns the final real clip path selected
// - empty if the tree contains no reachable real Clip node (shouldn't
// happen for a well-formed real state, but tolerated rather than thrown,
// matching this project's "a missing/malformed piece of content degrades
// gracefully" convention elsewhere).
//
// `preferLocomotion` - when true, actively prefers a real `Clip` node
// whose `punfParameterName == "locomotion"` over the state's own mood
// tree, when both are siblings under the same real `Speed` or legacy `Container` (the real
// shape of states like "loop_combat_standing", which bundle real walk/run
// clips alongside their own idle-variant tree) - used for real movement
// states. This legacy helper approximates a composite as one leaf; the
// normal offline runtime uses selectSpeedAnimationClips and LocomotionPlayback.
// false (the default) skips locomotion-tagged clips entirely,
// used for a real resting/idle selection.
std::string selectAnimationClip(const AnimationNode& root, const AnimationSelectionContext& context,
                                 bool preferLocomotion = false);

} // namespace assets
