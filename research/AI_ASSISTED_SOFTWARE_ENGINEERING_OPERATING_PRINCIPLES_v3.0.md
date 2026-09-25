AI ASSISTED SOFTWARE ENGINEERING
OPERATING PRINCIPLES v3.0

GENERAL PROJECT DEVELOPMENT FRAMEWORK FOR AI AGENTS

============================================================
PURPOSE
============================================================

This document defines a general, project independent operating framework
for AI assisted software development.

It is based on lessons learned from long term development of complex
software systems where AI agents are used to:

• understand requirements
• design architecture
• write code
• modify existing code
• create documentation
• execute tests
• debug problems
• perform audits
• review previous work
• hand work between agents
• maintain project state
• recover from failures
• evolve a system over time

This framework is NOT tied to any particular product, programming
language, architecture, platform, domain or technology stack.

The objective is to prevent recurring failure modes in future projects.

The fundamental principle is:

DO NOT OPTIMIZE ONLY FOR GENERATING CODE FASTER.

OPTIMIZE FOR PRESERVING CORRECTNESS WHILE GENERATING CODE.

A successful AI assisted development process must remain:

• understandable
• recoverable
• testable
• traceable
• controllable
• maintainable

even when development continues for a long time, across many agents,
many conversations, many revisions and many implementation stages.


============================================================
0. DOCUMENT AUTHORITY AND EXECUTION ENTRY
============================================================

This document is the sole authoritative engineering operating framework for
how AI assisted development is conducted.

The project specific Source Of Truth is the authoritative source for what the
project currently is.

These authorities are not competitors. They govern different layers:

• this framework defines how engineering work is conducted
• the project Source Of Truth defines the current project reality
• agent memory, conversation history and historical material are not authoritative

A project specific instruction may constrain project content only when it does
not violate this framework. A local convenience, agent inference or historical
statement may never silently override either governing layer.

If the framework and the project Source Of Truth appear to conflict, surface the
conflict, classify the layer in which the contradiction exists, preserve the
current state and resolve the governance conflict before continuing. Do not
silently merge the two sources.

This document is both the governing ruleset and the mandatory execution entry
point. An agent receiving this document must be able to determine what to do
before implementation starts, what evidence must exist during development and
what conditions are required before work may be considered closed.

Before performing development work, the agent must establish the project entry
state in the following order:

1. Read this document in full.
2. Identify the project root and the current authoritative project state.
3. Identify the current WORKLOG or project state record.
4. Follow the documentation reading order recorded there.
5. Identify the current Source Of Truth and distinguish current truth from
   historical, experimental and generated material.
6. Reconstruct the current implementation state, including uncommitted or
   generated changes where relevant.
7. Audit the current development environment, tools and dependencies using fresh
   authoritative external sources where facts may have changed.
8. Establish or verify the canonical project environment.
9. Determine the installation and bootstrap requirements before changing an
   installer or installation process.
10. Identify the documentation that must be updated during the current phase.
11. Only then begin planning or implementation.

The beginning of the WORKLOG or equivalent project state record must contain:

• documentation reading order
• authoritative documents and their roles
• current implementation state
• current phase
• current known limitations
• current open decisions
• current blockers
• current dependency and environment state
• current validation state
• exact next action

The WORKLOG is an operational project state record, not a second engineering
ruleset. It must not redefine the principles in this document. It records how
those principles currently apply to the specific project.

If the project does not yet have the required WORKLOG or equivalent state
record, the first project action is to create it with the required reading order
and current state before substantial implementation begins.

The agent must never rely on the chat conversation as the sole source of project
state. Every fact required for continuation, verification or recovery must be
represented in durable project artifacts.

The entry protocol applies to every development cycle, including small fixes,
documentation changes, configuration changes, dependency changes, installer
changes and audit driven corrections. The required understanding is constant,
but the execution depth and validation depth may be scaled according to change
risk and blast radius as defined by the master development lifecycle.

============================================================
1. PROJECT INITIALIZATION
============================================================

Before implementation begins, establish the project context explicitly.

At minimum define:

• project purpose
• project scope
• non goals
• architectural boundaries
• current implementation state
• Source Of Truth
• repository structure
• documentation structure
• versioning scheme
• testing strategy
• configuration strategy
• current known limitations
• known risks
• completed decisions
• open decisions
• project maturity

The agent must know what the project is and what it is NOT.

The agent must not silently infer missing architectural decisions.

If an important decision is missing, identify it explicitly.

A project should have a clearly defined starting state before substantial
implementation begins.

============================================================
2. SOURCE OF TRUTH
============================================================

Every serious project must have an explicit Source Of Truth.

The agent must know:

What is authoritative?

What is historical?

What is experimental?

What is generated?

What is implementation detail?

What is obsolete?

When two sources disagree, the conflict must be surfaced.

Never silently merge contradictory sources.

Never solve contradictions simply by choosing whichever source is easier
to implement.

Do not allow implementation convenience to redefine architectural truth.

============================================================
3. ARCHITECTURE VS IMPLEMENTATION
============================================================

Maintain a strict distinction between:

• Architecture
• Implementation
• Configuration
• Testing
• Diagnostics
• Documentation
• Temporary tooling
• Experimental work

An implementation detail must not silently become an architectural rule.

A temporary workaround must not silently become permanent architecture.

A test implementation must not become a production runtime dependency
unless explicitly decided.

A debugging mechanism must not become part of the production model
unless explicitly decided.

A convenience abstraction must not automatically become an architectural
component.

============================================================
4. ARCHITECTURAL OWNERSHIP
============================================================

Every important responsibility must have an owner.

For every major object, state, decision and operation determine:

Who creates it?

Who owns it?

Who may modify it?

Who may read it?

Who persists it?

Who destroys it?

Who is authoritative when conflicts occur?

If two components can both legitimately claim ownership, stop and
investigate.

Ambiguous ownership is one of the most common sources of architectural
drift.

Do not allow two components to perform the same responsibility merely
because the distinction was never explicitly defined.

============================================================
5. DATA OWNERSHIP
============================================================

Every important piece of information must have an explicit lifecycle.

For each important object determine:

Creation
Ownership
Read access
Mutation authority
Persistence
Expiration
Destruction

Also determine whether it is:

• mutable
• immutable
• ephemeral
• persistent
• derived
• cached
• authoritative

Do not confuse a cache with authoritative data.

Do not confuse a snapshot with a mutable source.

Do not allow derived data to silently become a source of truth.

============================================================
6. STATE MANAGEMENT
============================================================

Project state must never exist only inside the agent's conversational
memory.

Important project state must be represented explicitly.

At minimum distinguish:

• current architecture
• current implementation status
• current working version
• completed decisions
• open decisions
• known problems
• known limitations
• current task
• current subtask
• files modified
• tests performed
• tests still required
• unresolved findings
• rejected approaches
• assumptions
• known risks
• current checkpoint

A previous conversation is not a reliable persistence mechanism.

The agent must not assume that because something was discussed earlier,
it still exists in the current working tree.

============================================================
7. STATE PRESERVATION
============================================================

Long running AI development projects must preserve state outside the
conversation.

Important project state must be recoverable after:

• context loss
• agent restart
• model change
• handoff to another agent
• stale conversation
• repository reset
• accidental overwrite
• working directory change
• environment change

A future agent should be able to reconstruct:

Where are we?

What has already been decided?

What has already been implemented?

What must NOT be changed?

What remains open?

What was already tested?

What failed?

What is currently being worked on?

What is the last known good state?

============================================================
8. AGENT HANDOFF
============================================================

A handoff must transfer project state, not merely task instructions.

A good handoff should contain:

• current objective
• relevant architecture
• authoritative documents
• current implementation state
• recent changes
• known risks
• open questions
• completed work
• validation performed
• known failures
• exact next step
• protected constraints

Never assume the next agent will understand the previous conversation.

The receiving agent must be able to continue without reconstructing
the entire project history from chat logs.

============================================================
9. AGENT CONFIGURATION
============================================================

Agent configuration must be treated as part of the development process.

Do not wait until the agent starts behaving incorrectly before adding
important constraints.

At project initialization define:

• scope
• authority
• forbidden changes
• coding conventions
• documentation conventions
• testing expectations
• validation requirements
• state preservation requirements
• escalation rules
• change approval rules
• repository safety rules
• backup or checkpoint expectations
• reporting expectations

The agent should not have to rediscover these rules during development.

============================================================
10. AGENT MUST NOT INVENT MISSING BEHAVIOR
============================================================

When behavior is unspecified, the agent must distinguish:

"I know this."

"I infer this."

"I recommend this."

These are not the same.

Never silently convert an inference into project truth.

If implementation requires an architectural assumption that is not
documented, stop and identify it.

A missing specification is not permission to invent architecture.

============================================================
11. ASSUMPTION CONTROL
============================================================

Agents should explicitly list assumptions when they materially affect
implementation.

An assumption that later becomes important should be promoted into an
explicit project decision or specification.

Untracked assumptions are hidden architecture.

Hidden architecture is a future source of contradiction.

============================================================
12. DECISION MANAGEMENT
============================================================

Separate:

• Open decision
• Resolved decision
• Rejected proposal
• Historical decision
• Implementation choice
• Temporary experiment

When a decision is resolved, record:

• decision
• reason
• alternatives considered
• rejected alternatives
• affected components
• relevant revision

A future agent should not reopen the same discussion merely because it
cannot see why the original decision was made.

============================================================
13. REJECTED APPROACHES
============================================================

Important rejected approaches should sometimes be preserved.

Otherwise a future agent may independently rediscover the same idea and
waste time repeating an already rejected design.

However, rejected approaches must never be confused with current
architecture.

Historical reasoning is useful.

Historical architecture is not automatically current architecture.

============================================================
14. CHANGE DISCIPLINE
============================================================

Every meaningful change should have:

• reason
• scope
• affected components
• expected impact
• validation

Avoid broad uncontrolled modifications.

Prefer small coherent change sets.

Do not mix:

• architecture changes
• refactoring
• feature development
• formatting
• unrelated cleanup

unless there is a specific reason.

Before a large change, identify the expected blast radius.

============================================================
15. SNAPSHOT INTEGRITY
============================================================

A major problem in AI assisted development is stale snapshots.

An agent may modify an older copy of the project and accidentally
overwrite newer decisions.

Before modifying a large project, establish:

What is the current snapshot?

What changed since the snapshot?

Is this file authoritative?

Are previous modifications already integrated?

Is the working directory current?

Are there multiple copies of the same project?

Which copy is authoritative?

Never assume that a file with a familiar name is the newest version.

============================================================
16. FILESYSTEM AND ARTIFACT VERIFICATION
============================================================

Never trust an agent's own description of what it changed.

After significant work verify the actual artifacts.

Check:

• files created
• files deleted
• files modified
• file contents
• expected locations
• version headers
• generated artifacts
• package structure
• archive contents
• repository status where applicable

A report saying:

"Done"

is not validation.

The actual filesystem or repository state is the evidence.

============================================================
17. DO NOT OVER TRUST THE AGENT
============================================================

An agent can produce a coherent looking explanation that is still wrong.

Therefore:

Never validate an architecture only by asking the same agent whether
its architecture is correct.

Use:

• independent review
• adversarial review
• cross document consistency checks
• implementation validation
• tests
• fresh agent review
• real environment validation where applicable

Agent confidence is not evidence.

Good prose is not evidence.

A successful tool call is not evidence that the intended result was
actually achieved.

============================================================
18. INDEPENDENT AUDITS
============================================================

A major architecture should periodically be reviewed by an agent that
does not rely on the original reasoning history.

The independent auditor should:

• reconstruct the system from authoritative sources
• identify ownership
• identify dependencies
• identify lifecycle
• identify state
• identify persistence
• identify failure paths
• search for contradictions
• search for circular dependencies
• search for undocumented assumptions

The auditor should not automatically modify the system.

Correct process:

AUDIT
→ REPORT
→ DECISION
→ MODIFY
→ VERIFY
→ RE AUDIT

============================================================
19. RED TEAM AUDITS
============================================================

Normal review asks:

"Does this work?"

Red team review asks:

"How could this fail?"

The second question is essential.

Attack the architecture with:

• empty states
• first startup
• restart
• failure
• partial completion
• missing dependencies
• unexpected input
• concurrent operations
• stale data
• duplicate requests
• interrupted operations
• corrupted state
• invalid configuration
• migration
• recovery
• long running operations
• partial deployment
• version mismatch
• unavailable external dependencies

Do not test only the expected path.

============================================================
20. LIFECYCLE AUDIT
============================================================

For every important object or process trace:

Creation
→ initialization
→ active use
→ modification
→ completion
→ failure
→ recovery
→ destruction

A component that has a defined creation path but no defined failure
path is not fully specified.

A persistent object without a lifecycle is a future problem.

============================================================
21. BOOTSTRAP ANALYSIS
============================================================

Every project should explicitly answer:

What happens on first startup?

What exists before the first user interaction?

What does not exist yet?

Who creates the initial state?

What happens if the environment is empty?

What happens if configuration is missing?

What happens after restart?

What happens after partial initialization?

What happens if initialization fails?

Do not assume that normal runtime logic automatically works during
bootstrap.

Bootstrap deserves its own lifecycle analysis.

============================================================
22. ZERO STATE
============================================================

Every persistent system should define its zero state.

Do not confuse:

• no data
• no object
• empty object
• uninitialized object
• invalid object
• default object
• initial valid object

These are different states.

The existence of an empty or minimally populated object can be a valid
system state.

Zero state must be deliberately defined rather than accidentally
discovered during implementation.

============================================================
23. CIRCULAR DEPENDENCY AUDIT
============================================================

Always explicitly search for:

A requires B
B requires C
C requires A

Also search for indirect cycles involving:

• data
• state
• persistence
• initialization
• ownership
• configuration
• discovery
• execution
• learning
• validation
• recovery

A system can contain valid feedback loops.

Therefore distinguish:

• real circular dependency
• valid feedback loop
• documentation ambiguity
• implementation detail
• false positive

============================================================
24. HIDDEN OWNERSHIP AUDIT
============================================================

For every important object determine exactly who owns it.

For each object ask:

Who creates it?

Who modifies it?

Who reads it?

Who persists it?

Who invalidates it?

Who destroys it?

Who is authoritative?

If any answer is unclear, report the ambiguity.

============================================================
25. HIDDEN STATE AUDIT
============================================================

Search for state that exists conceptually but has not been formally
defined.

Examples:

• operation in progress
• background processing
• pending execution
• pending approval
• authentication window
• waiting for user
• stale state
• update pending
• dependency unavailable
• recovery in progress

Do not automatically create new states.

First determine whether the architecture already has a suitable place
for the state.

If it does not, report the gap.

============================================================
26. IMMUTABILITY AUDIT
============================================================

Identify every object described as immutable.

Then verify whether any other document or implementation path implies
that it can be modified.

Pay particular attention to:

• configuration snapshots
• knowledge snapshots
• committed requests
• execution plans
• generated plans
• external state snapshots
• session data
• cached data

Every immutability claim should have a clear ownership model.

============================================================
27. TEMPORAL AND ORDERING AUDIT
============================================================

Look for statements where the order of operations matters.

Examples:

A
→ B
→ C

If B must happen before C, that ordering should be explicit.

For every critical ordering rule determine whether it is:

• explicitly defined
• implied
• ambiguous
• contradictory

An undocumented ordering dependency is a hidden architectural dependency.

============================================================
28. FAILURE MODEL AUDIT
============================================================

A system can appear architecturally complete when only the happy path
is documented.

Therefore audit failure paths aggressively.

For each major component determine:

• expected failure
• unexpected failure
• timeout
• unavailable dependency
• invalid input
• partial result
• stale data
• retry behavior
• recovery behavior
• user visible behavior
• developer diagnostic behavior

Do not invent retry policies.

If unspecified, report the gap.

============================================================
29. RECOVERY AND ROLLBACK
============================================================

Define what happens after:

• failed implementation
• bad refactor
• accidental overwrite
• stale snapshot
• corrupted configuration
• failed migration
• failed test suite
• incorrect agent modification
• incorrect architectural change

The development process should have a known path back to the last
known good state.

Do not allow the agent to continue building on a known corrupted state
without explicit authorization.

============================================================
30. CHECKPOINTS
============================================================

Long tasks should have explicit checkpoints.

At meaningful milestones record:

• current state
• changes
• tests
• known issues
• decisions
• next step
• last known good state

Checkpoints are especially important when:

• many files are changing
• architecture is changing
• multiple agents are involved
• large refactors are occurring
• context windows are large
• work is performed over multiple sessions

============================================================
31. STOP CONDITIONS
============================================================

The agent must know when to stop.

Stop and ask for clarification when:

• two authoritative sources conflict
• a required architectural decision is missing
• implementation would require inventing behavior
• a change would affect an unrelated architectural boundary
• a requested modification violates a protected constraint
• a destructive operation is required but not explicitly authorized
• the current snapshot cannot be trusted
• the agent cannot establish which version is authoritative

Do not guess merely to maintain momentum.

If clarification or human decision is required and no human decision maker is
currently available:

• preserve the current state
• record the unresolved decision
• classify the work as BLOCKED or DEFERRED BY DECISION where appropriate
• create a recoverable checkpoint when work has already changed project state
• do not continue past the decision boundary by inventing behavior

============================================================
32. SCOPE CONTROL
============================================================

Agents tend to expand tasks.

Therefore every task should have:

Objective
In scope
Out of scope
Acceptance criteria

The agent must not solve adjacent problems unless explicitly authorized.

If an adjacent problem blocks the current task, report it rather than
silently expanding scope.

============================================================
33. AUTOMATION VS HUMAN DECISION
============================================================

Automate:

• repetitive checks
• consistency checks
• formatting
• deterministic validation
• regression execution
• file inventory
• version verification
• artifact verification

Require a human decision when the project's authority model requires one for:

• architectural changes
• conflicting requirements
• major scope changes
• security tradeoffs
• destructive operations
• unresolved ambiguity
• irreversible migration decisions

When no human decision maker is available, do not silently convert the task into
autonomous decision making. Preserve the state, record the decision required and
classify the cycle as blocked or deferred as appropriate.

============================================================
34. CONFIGURATION MANAGEMENT
============================================================

Configuration must be explicit.

The agent should know:

• where configuration lives
• which configuration is authoritative
• which values are defaults
• which values are user configurable
• which values are generated
• which values are environment dependent
• which values are secrets

Do not allow configuration to gradually become scattered across:

• code
• scripts
• documentation
• environment variables
• agent assumptions

============================================================
35. SECRETS AND SENSITIVE DATA
============================================================

Never place secrets into:

• source code
• documentation
• prompts
• logs
• test fixtures
• screenshots
• generated reports
• persistent state
• version control

The agent must distinguish technical state from sensitive state.

============================================================
36. VERSIONING
============================================================

Use explicit versioning.

Separate:

• architecture generation
• working revision
• product maturity
• implementation version
• dependency version where relevant

Do not use one number to represent unrelated concepts.

Version changes should communicate why the project changed.

Do not increment versions merely because an audit happened.

============================================================
37. DOCUMENTATION CONSISTENCY
============================================================

When an architectural rule changes, search every document that could
reference that rule.

Do not update only the document where the contradiction was discovered.

Perform cross document verification.

Terminology must also remain consistent.

One concept should not have several subtly different meanings unless
those meanings are explicitly defined.

============================================================
38. TERMINOLOGY CONTROL
============================================================

Define important terms once.

Avoid introducing synonyms casually.

For important concepts distinguish:

• state
• status
• phase
• event
• object
• command
• request
• intent
• plan
• result
• error
• failure
• configuration
• snapshot
• persistence

Terminology drift can create architectural ambiguity even when the
underlying implementation has not changed.

============================================================
39. TESTING STRATEGY
============================================================

Testing must not be an afterthought.

Define testing early.

Separate:

• unit testing
• component testing
• integration testing
• end to end testing
• regression testing
• smoke testing
• negative testing
• failure testing
• performance testing
• security testing
• recovery testing

Each test level should have a purpose.

Do not use end to end tests to compensate for missing lower level
coverage.

============================================================
40. TEST THE CONTRACTS
============================================================

Tests should verify architectural contracts, not only implementation.

For every important boundary verify where applicable:

• input
• output
• ownership
• mutability
• error behavior
• ordering
• timing
• persistence

A test that passes while violating the architectural contract is not
successful architectural validation.

============================================================
41. HAPPY PATH IS NOT ENOUGH
============================================================

For every major feature test at least:

• normal case
• empty case
• invalid case
• boundary case
• failure case
• recovery case
• repeated case
• restart case

Where relevant also test:

• concurrent case
• partial completion
• stale state
• missing dependency
• corrupted input
• configuration mismatch

============================================================
42. TEST ENVIRONMENT CONTROL
============================================================

A test result is only meaningful if the environment is known.

Record where appropriate:

• software version
• configuration
• dependencies
• operating environment
• test data
• execution method
• relevant external dependencies

Avoid conclusions based on an unknown or changing test environment.

============================================================
43. REGRESSION DISCIPLINE
============================================================

After meaningful changes:

Run targeted tests first.

Then run relevant regression tests.

After major changes perform broader validation.

Do not assume that because the modified component passes its tests,
the rest of the architecture remains intact.

============================================================
44. TEST SETUP AND BOOTSTRAP
============================================================

Setup and initialization must themselves be tested.

Verify:

• clean installation
• first startup
• default configuration
• empty environment
• partially configured environment
• invalid configuration
• restart after successful initialization
• restart after failed initialization
• migration from previous version

Bootstrap bugs are especially dangerous because they can prevent every
later test from being meaningful.

============================================================
45. AUDIT AFTER IMPLEMENTATION
============================================================

Documentation audit alone is insufficient.

Implementation audit alone is insufficient.

Compare:

Architecture
↔ Documentation
↔ Implementation
↔ Tests

The four must agree.

If documentation says one thing and code does another:

Do not silently rewrite the documentation to match the code.

Determine which is authoritative.

============================================================
46. REAL WORLD VALIDATION
============================================================

A system is not finished because:

• the code compiles
• unit tests pass
• the agent says it works
• documentation looks complete

Validate the actual user flow.

Where appropriate:

Setup
→ real input
→ real processing
→ real output
→ real environment

should be tested.

============================================================
47. OBSERVABILITY
============================================================

A system should be observable during development.

When something fails, the developer should be able to determine:

Where did it fail?

What was the input?

What component owned the operation?

What state existed?

What output was produced?

What happened next?

Diagnostics should help answer these questions without becoming a
hidden dependency of the production architecture.

============================================================
48. AGENT QUALITY CONTROL
============================================================

The agent itself should be treated as a fallible development tool.

Periodically evaluate:

• Did it follow the architecture?
• Did it modify only intended files?
• Did it preserve existing behavior?
• Did it introduce undocumented assumptions?
• Did it forget previous decisions?
• Did it incorrectly infer requirements?
• Did it test its changes?
• Did it report failures honestly?
• Did it actually verify its artifacts?
• Does its final report match the real project state?

Do not measure agent quality only by how good its prose looks.

============================================================
49. COMMON AI AGENT FAILURE PATTERNS
============================================================

The following patterns should be actively guarded against.

CONTEXT DRIFT

The agent gradually loses the original project constraints and starts
optimizing for the immediate task instead.

STALE SNAPSHOT MODIFICATION

The agent modifies an older copy of the project and accidentally
overwrites newer work.

ACCIDENTAL OVERWRITE

The agent recreates or replaces files without preserving newer changes.

DECISION LOSS

The agent forgets a previous architectural decision and independently
reopens the same problem.

ARCHITECTURE DRIFT

The agent changes architecture while claiming to perform only an
implementation task.

SYMPTOM FIXING

The agent fixes the visible problem instead of identifying the ownership,
lifecycle or dependency problem that caused it.

ABSTRACTION CREEP

The agent introduces unnecessary layers, interfaces, managers or
frameworks because they appear architecturally elegant.

SCOPE CREEP

The agent starts solving adjacent problems that were not part of the
task.

SILENT ASSUMPTION

The agent encounters missing behavior and invents an answer without
reporting the assumption.

CONTRADICTION MASKING

The agent silently chooses one contradictory source instead of reporting
the conflict.

SELF VALIDATION

The same agent declares its own architecture correct without independent
review.

FALSE COMPLETION

The agent says that work is complete without verifying the actual
artifacts.

REPORT REALITY GAP

The final report claims changes or tests that do not match the actual
filesystem, repository or test results.

TESTING THE MODIFIED FILE ONLY

The agent tests only the component it changed and misses integration
regressions.

DOCUMENTATION EQUIVALENCE ERROR

The agent assumes that complete documentation means a correct system.

CONVERSATION DEPENDENCY

Critical project state exists only in the conversation and disappears
when context is lost.

AUDIT LOOP

The team repeatedly audits the same decisions without reaching explicit
closure.

PREMATURE IMPLEMENTATION

The agent starts coding before architecture, ownership or lifecycle
questions are resolved.

IMPLEMENTATION BECOMES ARCHITECTURE

A temporary implementation choice becomes treated as an architectural
requirement.

TOOL SUCCESS FALLACY

A successful command or tool call is interpreted as proof that the
desired result was achieved.

CHANGE BLAST RADIUS IGNORANCE

The agent changes a shared abstraction without checking all consumers.

============================================================
50. DEVELOPMENT WORK CYCLE
============================================================

All development work follows the single canonical development lifecycle defined
in Section 75.

Section 50 does not define a second lifecycle. It establishes the operational
rule that every cycle must enter the master lifecycle, perform the minimum steps
required for its execution depth, and produce a truthful closure state.

Do not replace the lifecycle with:

READ
→ CHANGE EVERYTHING
→ DONE

A smaller task may use a smaller execution depth, but it must not skip the
required control points for its risk level.

51. PRE CHANGE CHECK
============================================================

Before modifying code or architecture, verify:

• correct repository
• correct branch or working state where applicable
• current project version
• relevant Source Of Truth
• task scope
• protected constraints
• affected components
• likely consumers
• existing tests
• known dependencies
• current known failures

============================================================
52. POST CHANGE CHECK
============================================================

After modification verify:

• intended files changed
• unintended files did not change
• architecture still matches documentation
• imports and dependencies are valid
• configuration remains valid
• tests pass
• relevant regression tests pass
• generated artifacts are correct
• version information is correct
• no protected constraint was violated

============================================================
53. LARGE TASK CHECKPOINTING
============================================================

For large tasks do not wait until the end to validate everything.

Use incremental checkpoints.

Example:

Phase 1
→ understand

Checkpoint

Phase 2
→ implementation

Checkpoint

Phase 3
→ targeted tests

Checkpoint

Phase 4
→ integration

Checkpoint

Phase 5
→ regression

Checkpoint

Phase 6
→ final audit

This limits the blast radius of mistakes.

============================================================
54. MULTI AGENT DEVELOPMENT
============================================================

Different agents may perform different roles:

• Builder
• Reviewer
• Tester
• Architect
• Red Team Auditor
• Documentation Auditor
• Debugger
• Release Validator

Do not assume that all agents share the same context.

Every handoff must explicitly transfer state.

Independent agents are particularly useful for finding assumptions made
by the original agent.

Do not let one agent's interpretation silently become authoritative simply
because it was the first interpretation.

============================================================
55. ROLE SEPARATION
============================================================

Where practical, separate:

Creation
Review
Validation
Audit

The person or agent that created a solution is not always the best entity
to determine whether the solution is correct.

Independent review is especially valuable after:

• architectural changes
• large refactors
• migrations
• security changes
• persistence changes
• lifecycle changes
• major dependency changes

============================================================
56. IMPLEMENTABILITY TEST
============================================================

Pretend a new senior developer has never seen the project.

Give them only the authoritative project material.

Ask:

Can they implement the system without inventing architecture?

Every question they must ask represents one of:

• missing specification
• ambiguity
• contradiction
• implementation detail

This is one of the strongest tests of documentation quality.

============================================================
57. ARCHITECTURE RECONSTRUCTION TEST
============================================================

Give an independent agent only the authoritative architecture and
documentation.

Ask it to reconstruct:

• components
• responsibilities
• ownership
• state
• data flow
• lifecycle
• persistence
• failure paths

Then compare its reconstruction against the intended architecture.

If two competent agents reconstruct substantially different systems
from the same documentation, the documentation is not sufficiently
precise.

============================================================
58. DO NOT AUDIT FOREVER
============================================================

Audits have diminishing returns.

The process should have explicit closure criteria.

An audit should end when:

• findings are classified
• critical issues are resolved
• decisions are recorded
• affected documentation is updated
• consistency is verified
• tests pass
• closure is recorded

Do not repeatedly reopen resolved decisions without new evidence.

============================================================
59. AUDIT CLASSIFICATION
============================================================

Every finding should be classified as one of:

REAL ARCHITECTURAL CONTRADICTION

DOCUMENTATION GAP

IMPLEMENTATION DETAIL

DESIGN OBSERVATION

Only the first two automatically represent specification problems.

Do not report a design preference as an architectural defect.

============================================================
60. FINDING SEVERITY AND ORTHOGONAL CLASSIFICATION
============================================================

Finding classification uses three independent dimensions:

TYPE

Use the types defined in Section 59:

• REAL ARCHITECTURAL CONTRADICTION
• DOCUMENTATION GAP
• IMPLEMENTATION DETAIL
• DESIGN OBSERVATION

SEVERITY

CRITICAL

The architecture cannot be implemented coherently without resolving the issue.

HIGH

Two authoritative rules conflict, a major lifecycle has no valid path, or the
issue creates substantial correctness, security, recovery or release risk.

MEDIUM

Important ambiguity or defect likely to cause inconsistent implementation or
meaningful validation risk.

LOW

Minor ambiguity, terminology issue or localized engineering concern.

INFORMATIONAL

Observation with no material architectural impact.

STATE

Use the audit finding lifecycle defined in Section 89:

OPEN
→ TRIAGED
→ DECISION
→ REMEDIATION
→ VERIFIED
→ CLOSED

or:

OPEN
→ ACCEPTED RISK

Type, severity and state are independent fields.

For example, a DESIGN OBSERVATION may be INFORMATIONAL, while a DOCUMENTATION
GAP may be HIGH. A finding does not become architectural merely because it has a
high severity label.

Do not inflate severity.
Do not use closure state as a substitute for severity.
Do not use severity as a substitute for decision state.

============================================================
61. AUDIT FIRST, MODIFY SECOND
============================================================

For an independent architectural audit:

First audit.

Then report.

Then decide.

Then modify.

Then verify.

Do not silently fix architectural problems during an audit unless the
task explicitly authorizes remediation.

This separation preserves the ability to distinguish:

What was originally wrong?

What did the auditor recommend?

What did the project owner decide?

What was eventually changed?

============================================================
62. PROTECTED CONSTRAINTS
============================================================

Projects should explicitly identify protected constraints.

Examples:

• number of canonical documents
• architectural boundaries
• persistence ownership
• security rules
• public interfaces
• compatibility requirements
• data formats
• supported platforms

The agent must verify these constraints before and after major changes.

============================================================
63. MIGRATION DISCIPLINE
============================================================

When replacing an existing architecture or implementation:

Explicitly identify:

• what is retained
• what is replaced
• what is deprecated
• what is removed
• what is historical
• what must never be restored

Do not accidentally reintroduce obsolete architecture because an old
implementation appears convenient.

Historical material is evidence, not automatically a specification.

============================================================
64. DEPENDENCY DISCIPLINE
============================================================

When adding or changing dependencies:

Determine:

• why it is required
• who consumes it
• whether it is runtime or development only
• whether it changes architecture
• whether it changes deployment
• whether it changes testing
• whether it creates a new failure mode
• whether it creates licensing or security implications

Do not add dependencies simply because they make implementation easier.

============================================================
65. PERFORMANCE DISCIPLINE
============================================================

Performance requirements should be explicit.

Distinguish:

• functional correctness
• latency
• throughput
• memory
• CPU
• startup time
• resource usage

Do not introduce premature optimization into architecture.

Do not ignore performance when it is part of the actual product requirement.

============================================================
66. SECURITY DISCIPLINE
============================================================

Security must be considered during architecture and implementation,
not only during final testing.

For important data and operations determine:

• trust boundary
• authentication
• authorization
• sensitive data
• storage
• transport
• logging
• failure behavior
• recovery

Do not allow debugging convenience to weaken security silently.

============================================================
67. OBSERVABILITY VS ARCHITECTURE
============================================================

Diagnostics are valuable but should remain distinguishable from
production business logic.

A diagnostic mechanism should not silently become:

• a new persistence layer
• a new source of truth
• a hidden state machine
• a new execution path
• a semantic dependency

unless explicitly designed that way.

============================================================
68. CONFIGURATION VALIDATION
============================================================

Configuration should be validated before runtime behavior depends on it.

A system should distinguish:

• missing configuration
• invalid configuration
• default configuration
• user configured value
• environment generated value
• secret value

Configuration errors should be visible and diagnosable.

============================================================
69. CLEAN ENVIRONMENT TEST
============================================================

Periodically validate that the system can be reconstructed from its
documented and versioned sources.

Do not rely on hidden local state.

A clean environment test can reveal:

• undocumented dependencies
• missing files
• local configuration assumptions
• forgotten generated artifacts
• undeclared packages
• undocumented setup steps

============================================================
70. REPRODUCIBILITY
============================================================

A serious project should strive for reproducible development and testing.

A future agent should be able to determine:

• what version was used
• what configuration was used
• what dependencies were used
• what commands were run
• what tests were executed
• what result was obtained

If a result cannot be reproduced, label it appropriately.

============================================================
71. FINAL PRE IMPLEMENTATION GATE
============================================================

Before a large implementation begins, verify:

Architecture defined
Source of Truth defined
Scope defined
State defined
Ownership defined
Lifecycle defined
Persistence defined
Failure paths defined
Testing strategy defined
Versioning defined
Configuration defined
Recovery defined
Open decisions identified
Known ambiguities identified
Protected constraints identified
Validation strategy defined

If major items are missing, resolve them before large scale implementation.

============================================================
72. FINAL PRE RELEASE GATE
============================================================

Before a significant release or milestone verify:

• architecture consistency
• documentation consistency
• implementation correctness
• regression results
• configuration
• security
• performance where required
• recovery behavior
• deployment reproducibility
• artifact integrity
• version correctness
• known limitations
• unresolved risks

============================================================
73. FINAL PROJECT HEALTH CHECK
============================================================

At major milestones ask:

Can a new agent understand the project?

Can a new developer understand the project?

Can the project be restored after context loss?

Can the current architecture be reconstructed independently?

Can the system be tested reproducibly?

Can failures be diagnosed?

Can previous decisions be recovered?

Can obsolete decisions be distinguished from current ones?

Can implementation be traced back to requirements and architecture?

If several answers are NO, the project has accumulated process debt.

============================================================
74. CORE PRINCIPLES
============================================================

The most expensive problems in AI assisted development are often not
coding bugs.

They are:

• wrong assumptions
• lost context
• stale state
• ambiguous ownership
• contradictory documentation
• undocumented lifecycle
• untracked decisions
• missing tests
• architecture drift
• silent agent inference
• uncontrolled scope
• false completion
• insufficient verification

Therefore the development process should always prefer:

clarity over guessing

traceability over memory

validation over confidence

explicit state over implicit context

small changes over uncontrolled changes

independent review over self validation

documented decisions over repeated debate

recoverability over convenience

reproducibility over local assumptions

evidence over agent confidence

============================================================
75. THE MASTER DEVELOPMENT LIFECYCLE
============================================================

All development work uses one canonical lifecycle.

ENTER
    ↓
RECONSTRUCT CURRENT STATE
    ↓
ESTABLISH AUTHORITY
    ↓
ESTABLISH CONSTRAINTS
    ↓
ASSESS IMPACT AND BLAST RADIUS
    ↓
PLAN
    ↓
IMPLEMENT OR CHANGE
    ↓
VERIFY ARTIFACTS
    ↓
TEST
    ↓
REVIEW
    ↓
AUDIT WHEN REQUIRED
    ↓
UPDATE DOCUMENTATION AND STATE
    ↓
CHECKPOINT
    ↓
PACKAGE
    ↓
VERIFY HANDOFF ARTIFACT
    ↓
REPORT CLOSURE STATE

This is the only canonical development lifecycle in this document.
No later section may define a competing master loop.

Execution depth is scaled by risk, blast radius and change type:

MINIMAL

Use for genuinely isolated, low risk work with no architectural or shared
contract impact. The agent still establishes current state, preserves authority,
updates documentation, verifies the change, records the result and produces the
required recoverable package.

STANDARD

Use for normal feature work, bug fixes with integration impact, dependency
changes, configuration changes, installer changes and other work with meaningful
consumer or environment impact.

DEEP

Use for architectural changes, security sensitive changes, major migrations,
large refactors, shared contract changes, uncertain root causes and other work
with substantial blast radius. Deep execution may require red team review,
minimal reproductions, A B validation, broader regression and additional
checkpoints.

Scaling the execution depth never removes the mandatory closure requirements.
Every cycle must still end in documented, verified and recoverable state.

For architectural work, the lifecycle remains the same. The additional analysis
is performed inside RECONSTRUCT, ASSESS IMPACT, REVIEW and AUDIT rather than by
creating a second master loop.

76. FINAL PRINCIPLE
============================================================

The objective is not merely to build software.

The objective is to build software through a process where the project
remains understandable, recoverable, testable, traceable and controllable
even when AI agents are responsible for substantial parts of the
development.

An AI agent should never be treated as:

an infallible architect
an infallible developer
an infallible tester
an infallible historian
or an infallible source of truth.

The agent is a powerful development instrument.

The project architecture, explicit state, documented decisions,
verification, tests and independent review provide the control system.

The most important operational rule is therefore:

NEVER LET THE AGENT'S CURRENT CONTEXT BECOME THE ONLY PLACE WHERE
PROJECT TRUTH EXISTS.
============================================================
77. FINAL STATE VALIDATION
============================================================

Validation must be performed against the exact state that will be released,
deployed or handed off.

The validation sequence should ensure:

implementation
→ version update
→ generated metadata update
→ final validation
→ packaging
→ artifact verification

The final artifact must originate from the exact state that passed the final
validation gate.

Never assume that:

"the code passed tests"

means:

"the released artifact passed tests."

A test executed before the final version, generated files or packaging changes
is not evidence for the final artifact.

Where practical, record together:

• exact revision
• final tree state
• package identifier
• package hash
• validation result

A release gate that validates an earlier state must not be presented as
validation of the final released state.

============================================================
78. SEMANTIC VALIDATION
============================================================

A passing implementation test does not automatically prove that the
implemented concept has the intended meaning.

For every important domain concept distinguish:

• mechanism
• representation
• semantics
• observable behavior

Examples:

A counter increasing does not prove that it represents observations.

A confidence value changing does not prove that it represents the intended
confidence semantics.

A timestamp changing does not prove that it represents the intended event time.

A record disappearing from retrieval does not prove that it expired.

A score changing does not prove that the user visible or domain outcome improved.

For important domain concepts, tests should include semantic scenarios that
verify the meaning of the resulting state, not only the mechanics that update
it.

When a field has a domain name such as confidence, state, validity, priority
or occurrence, verify that its actual behavior matches the meaning implied by
that name.

If the implementation is deliberately narrower than the natural meaning of
the term, document that limitation explicitly.

============================================================
79. DATA PROPAGATION AND INFORMATION LOSS
============================================================

Important information must be traceable across architectural boundaries.

When data moves through:

storage
→ domain model
→ service
→ result
→ renderer
→ API
→ UI
→ external consumer

determine which fields are:

• created
• preserved
• transformed
• aggregated
• intentionally omitted
• accidentally discarded

A value being correctly calculated is not sufficient if a later layer silently
discards it.

Every important cross layer contract should explicitly define:

• what enters
• what leaves
• what may be transformed
• what may be omitted
• why an omission is intentional

Silent information loss at a boundary is an architectural defect unless the
omission is intentional, documented and compatible with the contract.

When debugging a missing value, trace the value across every boundary rather
than assuming that the producing component is the failing component.

============================================================
80. CANONICAL PATH BEFORE PARALLEL PATHS
============================================================

Before introducing a second API, pipeline, retrieval path or subsystem,
determine whether the existing canonical path is already sufficient.

Prefer:

one canonical implementation
+
well defined result contracts
+
compatibility wrappers

over:

multiple partially overlapping implementations.

A second path should only exist when it has a clearly different:

• semantic purpose
• lifecycle
• ownership
• performance requirement
• compatibility requirement

Do not create parallel architecture merely because the existing path is
missing a consumer, interface or presentation layer.

First ask whether the existing canonical path already computes the required
information and only fails to expose or consume it.

Duplicate paths create additional sources of truth, additional test matrices
and additional opportunities for behavioral divergence.

============================================================
81. IMPLEMENTATION CLAIMS MUST BE QUALIFIED
============================================================

The following states are not equivalent:

• implemented
• tested
• verified
• integrated
• consumed
• deployed
• released
• field validated

A feature may be implemented but unused.

A value may be calculated but never consumed.

A test may pass while production integration remains broken.

A package may exist without having been deployed.

A release may exist without having been validated against the exact final
artifact.

Reports should qualify claims explicitly.

Where meaningful, use a status chain such as:

DESIGNED
→ IMPLEMENTED
→ UNIT TESTED
→ INTEGRATION TESTED
→ SYSTEM VERIFIED
→ DEPLOYED
→ FIELD VERIFIED

Do not collapse these states into a single statement such as "done".

============================================================
82. RECOVERABLE COMPLETION
============================================================

A meaningful development task is not complete merely because:

• code was changed
• tests passed
• a commit exists
• an agent reported success

At the end of every meaningful implementation phase, create a versioned
recoverable artifact outside transient workspace state.

Where applicable it should contain:

• source or changed files
• configuration
• tests
• documentation
• worklog
• exact revision
• validation evidence
• rollback instructions
• checksums

The recovery artifact must be independently verifiable.

Repository history is valuable, but repository history alone is not a
sufficient recovery mechanism for long running AI assisted development.

A repository may be unavailable, a workspace may reset, a branch may move, or
a generated artifact may no longer exist.

The recovery package provides an additional independently verifiable
checkpoint.

If completed work cannot be independently recovered, the work is not
considered fully closed.

============================================================
83. ENVIRONMENTAL RESULT QUALIFICATION
============================================================

A test result must be interpreted together with the environment in which it
was produced.

Do not automatically classify an environment failure as a product failure.

Do not automatically classify an environment limitation as evidence that the
product works.

When a result differs between environments, first compare:

• operating system
• runtime version
• interpreter version
• installed dependencies
• model or asset availability
• configuration
• environment variables
• filesystem state
• network availability where relevant
• hardware capabilities

Then determine whether the difference is:

• product behavior
• environment behavior
• test setup behavior
• infrastructure behavior
• unknown

Do not modify production code merely to make an environment specific test pass
until the cause has been established.

When a test is skipped because a dependency is unavailable, preserve the fact
that it was skipped. A skipped test is not equivalent to a passing test.

============================================================
84. CONTRACT CONTINUITY ACROSS BOUNDARIES
============================================================

Architectural contracts must remain coherent as information crosses component
boundaries.

For important outputs determine:

producer
→ contract
→ transformer
→ consumer

Verify that each transformation preserves the properties required by the next
consumer.

Do not allow a component to expose a rich result while a downstream layer
silently reduces it to a weaker representation without deliberate reason.

When a boundary intentionally reduces information, record:

• what was removed
• why it was removed
• whether the information can be recovered elsewhere
• whether the reduction is reversible

A boundary that changes semantics should be treated as an architectural
transformation, not merely a formatting operation.

============================================================
85. NEGATIVE SEMANTICS AND STATE TRANSITIONS
============================================================

Systems that store knowledge, state or durable information must explicitly
consider negative and corrective statements.

Do not validate only:

positive assertion
→ store
→ retrieve

Also examine:

assertion
→ correction
→ contradiction
→ supersession
→ invalidation
→ restoration

A statement such as:

"X is true"

and later:

"X is no longer true"

must not automatically become:

"X is more strongly true"

merely because the two representations are semantically similar.

Whenever a domain contains assertions that can change over time, include
negative, corrective and contradictory cases in the semantic test suite.

============================================================
86. RELEASE EVIDENCE CHAIN
============================================================

A release should have an evidence chain connecting:

source
→ revision
→ tests
→ generated artifacts
→ package
→ checksum
→ publication
→ deployment where applicable

Each transition should be attributable to an identifiable state.

Do not treat a release page, package filename or successful upload as proof
that the intended source revision was packaged.

Where practical, record:

• source revision
• version
• artifact name
• artifact checksum
• test result
• release metadata
• publication or mirror revision

The objective is to make the statement:

"this artifact was produced from this validated source state"

independently verifiable.

============================================================
87. CHANGE CONTAINMENT AND BLAST RADIUS
============================================================

Before modifying a shared component, identify its consumers.

For each shared change determine:

• direct consumers
• indirect consumers
• public interfaces
• configuration dependencies
• test dependencies
• generated artifacts
• deployment dependencies

A change is not small merely because the patch is small.

A one line change to a shared contract can have a larger blast radius than a
multi file change isolated to one component.

Prefer targeted changes with explicit consumer validation over apparently
small changes made without dependency analysis.

============================================================
88. PROOF OF ABSENCE REQUIREMENTS
============================================================

Some engineering claims are negative claims.

Examples:

• no external runtime dependency
• no duplicate implementation
• no remaining cloud call
• no stale source path
• no destructive delete path
• no new regression

Negative claims require an explicit search or test strategy.

Do not infer absence merely because the expected code path was not seen.

For important negative claims, define how absence was established:

• source search
• dependency graph
• runtime trace
• network observation
• test
• clean environment verification

A statement such as "there is no X" should be backed by an appropriate method
for proving absence.

============================================================
89. AUDIT FINDING CLOSURE
============================================================

An audit finding should have the STATE dimension defined in Section 60.

Use:

OPEN
→ TRIAGED
→ DECISION
→ REMEDIATION
→ VERIFIED
→ CLOSED

Optionally:

OPEN
→ ACCEPTED RISK

when the project owner explicitly accepts the remaining risk.

Do not close a finding merely because code was changed.

Verification must demonstrate that the original finding was actually addressed.

A finding should retain:

• original observation
• severity
• decision
• remediation
• verification evidence
• closure state

This prevents audit history from becoming indistinguishable from current
architecture.

============================================================
90. DESIGN PREFERENCE VS ARCHITECTURAL DEFECT
============================================================

Not every design difference is a defect.

Before classifying a finding as an architectural problem, determine whether it
is actually:

• a correctness failure
• a contract violation
• a lifecycle gap
• an ownership conflict
• a documentation gap
• a compatibility problem
• a maintainability concern
• a performance issue
• a security issue
• a design preference

Do not promote personal architectural preference into project truth.

An auditor should identify where the current system violates an explicit
requirement or creates demonstrable risk.

A proposal for a cleaner architecture is not automatically evidence that the
current architecture is wrong.

============================================================
91. CLOSURE MUST INCLUDE DECISION STATE
============================================================

At the end of a meaningful task, distinguish between:

DONE

and:

DONE WITH KNOWN LIMITATION

and:

BLOCKED

and:

DEFERRED BY DECISION

and:

ACCEPTED RISK

and:

UNVERIFIED

Do not report an item simply as completed when an important part remains
untested or deliberately deferred.

A truthful partial completion state is better than a misleading complete
state.

============================================================
92. THE ENGINEERING EVIDENCE CHAIN
============================================================

For important work, maintain an evidence chain:

REQUIREMENT
→ ARCHITECTURAL DECISION
→ IMPLEMENTATION
→ TEST
→ VERIFICATION
→ ARTIFACT
→ RELEASE

Where practical, a reviewer should be able to move backwards through this
chain.

This creates traceability between what the project intended, what was built,
what was tested and what was released.

If a link is missing, report the gap rather than inventing the missing
relationship.

============================================================
93. FINAL AI ENGINEERING PRINCIPLE
============================================================

The most dangerous AI engineering failure is not necessarily incorrect code.

It can be a coherent looking implementation that:

• solves a different problem than intended
• satisfies a test without satisfying the semantic contract
• calculates information but loses it at a boundary
• modifies a stale snapshot
• relies on an unverified environment assumption
• reports completion before release verification
• creates a second path instead of fixing the canonical path
• silently converts an inference into architecture

Therefore the development process must continuously distinguish:

WHAT EXISTS
from
WHAT IS INTENDED

WHAT IS IMPLEMENTED
from
WHAT IS CONSUMED

WHAT IS TESTED
from
WHAT IS VERIFIED

WHAT IS VERIFIED
from
WHAT IS RELEASED

WHAT IS TRUE
from
WHAT IS INFERRED

WHAT IS CURRENT
from
WHAT IS HISTORICAL

The agent may perform much of the work.

The engineering system must remain stronger than the agent's current
context.

============================================================
94. EXPERIMENTAL ISOLATION BEFORE ARCHITECTURAL CHANGE
============================================================

When a suspected architectural problem can be reproduced in a smaller system,
create an isolated experiment before modifying the production architecture.

The experiment should preserve the relevant mechanism while removing unrelated
complexity.

The purpose is not to build a second product.

The purpose is to answer one causal question:

"Does this mechanism actually produce the observed behavior?"

For difficult problems prefer:

OBSERVE
→ MINIMAL REPRODUCTION
→ CONTROLLED VARIATION
→ COMPARE
→ IDENTIFY CAUSAL CONDITION
→ THEN MODIFY PRODUCTION

A successful minimal reproduction is evidence.

A successful production modification without causal isolation is not
necessarily evidence.

============================================================
95. A/B ARCHITECTURAL VALIDATION
============================================================

When a change may affect a fragile architecture, prefer a reversible A/B path
before replacing the existing path.

The two paths should share as much behavior as possible while differing only in
the suspected cause.

Examples include:

• old rendering path vs experimental rendering path
• old state transition vs proposed state transition
• existing pipeline vs canonicalized pipeline
• current configuration vs isolated configuration

The experiment should answer:

Does the suspected change improve the target behavior without changing
unrelated behavior?

Do not convert an experiment into permanent architecture until both functional
and non functional effects are understood.

============================================================
96. MINIMAL REPRODUCIBLE ARCHITECTURE
============================================================

A useful debugging reproduction should preserve architectural characteristics,
not merely reproduce the visible symptom.

For example, when investigating rendering, concurrency, persistence or event
ordering, the reproduction should preserve the relevant execution model:

• same rendering primitive
• same ownership relationship
• same lifecycle
• same concurrency boundary
• same persistence boundary
• same environment constraint

A visually similar mock that removes the suspected mechanism is not a valid
architectural reproduction.

A reproduction is stronger when it also allows individual layers or conditions
to be enabled and disabled independently.

============================================================
97. DEFECT FIRST VALIDATION
============================================================

A regression test suite should be capable of proving that it would fail without
the fix when practical.

For important defects, establish:

KNOWN DEFECT
→ TEST EXPECTED TO FAIL
→ APPLY FIX
→ TEST PASSES

This distinguishes:

"the test passes"

from:

"the test actually proves the defect is fixed."

A test that is permanently green but never demonstrates sensitivity to the
original defect may be testing the wrong seam, the wrong state or only a
simulation of the intended behavior.

============================================================
98. TEST SEAM VALIDITY
============================================================

A test is only meaningful if it exercises the same responsibility boundary as
production.

When a test mocks, patches or replaces a dependency, verify that the production
code actually reaches that seam.

Do not assume that replacing a symbol proves that every real call site uses
that symbol.

For important UI, integration and callback behavior, prefer at least one test
that exercises the actual production call path.

A mock that intercepts a hypothetical path can create false confidence.

============================================================
99. TRANSIENT EXECUTION STATE
============================================================

Systems that execute work from a persistent collection often require a
separate transient execution context.

Do not assume that the persistent collection itself defines the active run.

Distinguish explicitly between:

• persistent items
• selected items
• eligible items
• active run members
• currently executing item
• completed items
• cancelled items
• items never included in the run

Operations such as pause, resume and stop should normally apply to the active
execution context rather than indiscriminately mutating the entire persistent
collection.

When an operation finishes, all transient execution state must reach a defined
terminal state.

This includes where applicable:

• running state
• paused state
• stop requested state
• current item
• current index
• restart state
• active run membership

Leaving transient state uncleared creates failures that appear unrelated to
the original operation.

============================================================
100. TERMINAL STATE CLEANUP
============================================================

Every asynchronous or stateful operation must define its terminal transition.

The terminal path should explicitly establish:

• what becomes inactive
• what is reset
• what remains persistent
• what callbacks are still allowed
• what late callbacks must do
• what the user interface should display
• what controls become enabled or disabled

Do not rely on one boolean such as "running" to imply that all related state
has been reset.

A system can be logically finished while stale auxiliary state still describes
an operation that no longer exists.

Terminal state should therefore be tested as a complete state, not merely as a
single flag.

============================================================
101. LATE CALLBACK AND IDENTITY VALIDATION
============================================================

Asynchronous systems must assume that completion events can arrive after the
logical relationship that created them has changed.

When multiple operations may exist simultaneously or sequentially, callbacks
should be associated with the specific operation instance they belong to.

Use explicit identity where appropriate:

• job identity
• request identity
• generation identity
• session identity
• version identity

Do not assume that "the current item" is still the item that originally
created a callback.

Late completion must either be safely ignored or explicitly reconciled.

============================================================
102. PARALLEL PIPELINE SEMANTIC EQUIVALENCE
============================================================

When two product paths claim to implement the same user visible behavior,
verify semantic equivalence rather than only implementation similarity.

Examples:

• Preview vs Batch
• interactive execution vs background execution
• single item vs bulk execution
• import vs manually created data
• cached vs uncached path

For shared semantics define canonical inputs and compare the meaningful output
contract.

Do not assume two paths remain equivalent merely because both call the same
lower level component.

A duplicated interpretation of the same concept is a high risk source of
divergence.

Prefer one canonical interpretation and adapters for the different execution
contexts.

============================================================
103. IMMUTABLE INPUTS AND MATERIALIZED REPRESENTATIONS
============================================================

When execution requires transforming user or domain input into another form,
distinguish between:

• original input
• derived representation
• execution representation

Prefer producing a derived representation without silently mutating the
original source of truth unless mutation is explicitly part of the contract.

This is especially important when the same input may be consumed by multiple
pipelines.

A transformation that mutates shared input can create order dependent bugs,
double application and cross path divergence.

============================================================
104. UI BUGS REQUIRE RUNTIME EVIDENCE
============================================================

Visual and interaction defects should not be diagnosed from source inspection
alone.

For UI problems, static analysis should be combined with runtime evidence.

Where practical capture:

• exact application state
• operating system
• display scaling
• font configuration
• rendering backend
• relevant widget hierarchy
• screenshots or recordings
• reproducible interaction sequence

A visual defect may depend on composition, DPI, graphics backend, theme,
window state or timing even when the source code appears correct.

A UI fix is not validated until the actual rendered result has been checked in
the affected environment.

============================================================
105. RENDERING PATHS ARE ARCHITECTURAL DEPENDENCIES
============================================================

When an application combines multiple rendering systems or multiple visual
composition stages, the rendering model itself becomes an architectural
dependency.

Do not reason only about which component draws a visual element.

Also determine:

• where it is rasterized
• which surface receives it
• whether it is composited
• which layer performs final composition
• whether transparency changes the path
• whether device scaling changes the path
• whether the result is filtered or resampled
• which runtime and graphics environment participates

A visual element may render correctly in isolation while appearing degraded after
composition, scaling or final presentation.

This is a framework level principle and is not tied to a particular GUI toolkit,
graphics API or platform.

============================================================
106. ENVIRONMENT SENSITIVE VISUAL VALIDATION
============================================================

Visual output should be compared under controlled environment conditions.

When relevant record:

• operating system
• display scaling
• device pixel ratio
• graphics driver
• rendering backend
• font installation
• application configuration
• relevant runtime versions

When a visual defect changes with scaling or graphics configuration, treat the
environment as part of the reproduction rather than noise.

These are general validation rules. They do not prescribe a particular toolkit
or rendering technology.

============================================================
107. DO NOT FIX VISUAL SYMPTOMS BEFORE THE RENDER PATH
============================================================

When text, icons or other UI elements appear degraded, do not immediately
compensate by changing:

• font family
• font size
• font weight
• color
• spacing
• asset resolution

First determine whether the degradation occurs during source rendering,
composition, scaling, filtering or final presentation.

Otherwise a rendering defect can become permanently encoded into visual design
choices that only hide the underlying problem.

108. ARCHITECTURAL CHANGE RISK GATING
============================================================

Before modifying a mature UI, persistence layer, concurrency model or shared
subsystem, explicitly classify the change by blast radius.

Low risk:

• isolated implementation detail
• no shared contract change
• no ownership change
• no lifecycle change

Higher risk:

• shared widget hierarchy
• state machine
• persistence format
• canonical execution path
• public interface
• rendering architecture
• cross component ownership

For higher risk changes require:

AUDIT
→ MINIMAL EXPERIMENT
→ REVERSIBLE IMPLEMENTATION
→ TARGETED VALIDATION
→ REGRESSION
→ FINAL DECISION

Do not perform architectural rewrites merely because a smaller experiment has
not yet been attempted.

============================================================
109. EVIDENCE BEFORE REFACTOR
============================================================

A cleaner architecture is not by itself evidence that the current architecture
is wrong.

Before refactoring a mature subsystem, establish:

• the observed failure
• the affected boundary
• the causal mechanism
• the minimum change that could address it
• the regressions the change could create
• the rollback path

Only then decide whether the problem requires refactoring, containment, or a
localized correction.

Prefer the smallest architectural change that removes the demonstrated cause
while preserving unrelated behavior.

============================================================
110. COMPLEXITY BUDGET FOR AI GENERATED CHANGES
============================================================

Every additional abstraction, state, path, layer or configuration option adds
future reasoning cost.

Before introducing one, ask:

What problem does this solve?

What existing mechanism was considered?

What new state does it introduce?

What new failure modes does it create?

What tests become necessary?

What ownership becomes necessary?

What can be removed if this experiment fails?

Do not allow an agent to trade a small local defect for a large increase in
system complexity.

============================================================
111. FINAL LESSON FROM LONG RUNNING AI DEVELOPMENT
============================================================

In long running AI assisted projects, many difficult defects were not caused
by an inability to write code.

They were caused by the interaction of otherwise reasonable components:

• two paths interpreting the same concept differently
• a persistent collection being mistaken for an active execution set
• transient state surviving a terminal transition
• a test seam not matching the real production path
• a visual defect being diagnosed without isolating the rendering chain
• a small implementation change having an unexpectedly large blast radius
• a plausible workaround being mistaken for a root cause

Therefore the agent should continuously ask:

WHAT IS THE CANONICAL PATH?

WHAT IS THE ACTIVE STATE?

WHO OWNS THIS STATE?

WHAT INFORMATION CROSSES THIS BOUNDARY?

WHAT EXACTLY WAS TESTED?

DID THE TEST EXERCISE THE REAL PATH?

CAN I REPRODUCE THE FAILURE IN A SMALLER SYSTEM?

WHAT IS THE SMALLEST REVERSIBLE CHANGE THAT CAN PROVE THE HYPOTHESIS?

WHAT ELSE COULD THIS CHANGE BREAK?

These questions should precede broad refactoring whenever the system is mature
and the observed defect may have multiple interacting causes.

============================================================


============================================================
112. DEVELOPMENT START AUDIT
============================================================

Before implementation, installation, dependency changes or substantial tooling
changes, perform a current development environment audit.

Use fresh authoritative internet sources where the information may have changed.
Do not rely on memory when version, compatibility, security, maintenance status,
installation method or supported platform information can be verified directly.

For each relevant runtime, tool, framework, library, package, plugin, SDK,
model, external service or build dependency determine where applicable:

• installed version
• requested version
• latest stable generally available version
• latest appropriate and supportable version
• compatibility with the project
• known security issues
• important breaking changes
• maintenance status
• official source
• official installation method
• runtime or development role
• transitive dependency impact
• licensing implications
• whether the dependency is actually necessary
• whether a project local alternative already exists

Latest does not automatically mean appropriate.

Prefer the newest version that is compatible, supportable, reproducible and
appropriate for the project. If the newest version is deliberately not used,
record the reason in the WORKLOG and relevant dependency documentation.

Record the audit date and the sources used for decisions that may later become
stale.

Do not begin implementation while a dependency or tool choice that materially
affects the design remains an unexamined guess.

============================================================
113. CANONICAL DEVELOPMENT ENVIRONMENT
============================================================

The project must have one clearly defined canonical development environment.

Project dependencies should remain inside the project controlled environment or
inside a clearly defined project specific environment that can be reproduced.

Do not scatter project dependencies across the target machine merely because a
global installation is convenient.

Global tools are allowed only when there is a documented reason that the tool
cannot or should not be project local.

For every exception record:

• why global installation is required
• which version is required
• how it is verified
• how another agent reproduces the requirement
• whether it affects the installer or runtime

The project must distinguish clearly between:

• project runtime dependencies
• project development dependencies
• installer bootstrap dependencies
• host system prerequisites
• optional external tools
• user specific configuration
• secrets

A dependency that exists only because it happens to be installed on the current
machine must not be treated as a project dependency.

The project should prefer explicit manifests and lock information where the
technology supports them. The recorded dependency state must be sufficient to
reconstruct the intended environment.

============================================================
114. INSTALLATION INFORMATION GATE
============================================================

Do not write an installer before collecting the information required to perform
a complete installation.

Before implementation of the installer determine:

• operating system requirements
• architecture requirements
• required runtimes
• required SDKs
• required system components
• project dependencies
• dependency versions
• dependency sources
• download locations
• integrity or checksum requirements where available
• required environment variables
• configuration files
• generated files
• required directories
• permissions
• elevated privilege requirements
• network requirements
• authentication requirements
• optional components
• post installation actions
• startup requirements
• validation commands
• cleanup and rollback actions

The installer specification must exist before installer implementation is treated
as complete.

If a required installation fact cannot be established, record the gap instead of
inventing it.

============================================================
115. ONE ACTION INSTALLATION
============================================================

The default installation experience is one user initiated action.

The user should not be required to:

• launch several installation files manually
• determine installation order
• discover hidden dependencies
• create required directories manually
• edit configuration files manually
• discover required commands
• set hidden environment variables manually
• repair predictable installation failures manually

The installer should perform the supported installation sequence itself.

The installation process should be:

IDEMPOTENT
→ DETECT CURRENT STATE
→ INSTALL OR REPAIR
→ CONFIGURE
→ VALIDATE
→ REPORT

Rerunning the installer on an already configured environment must be handled
intentionally rather than treated as an exceptional case.

A partial installation must not leave the project in an undocumented state.
Where practical the installer must support safe retry or rollback.

If manual interaction is unavoidable, document exactly why it is unavoidable,
what the user must do, and which parts remain automated.

============================================================
116. BOOTSTRAP AND INSTALLATION VALIDATION
============================================================

The installer and bootstrap process are part of the product development system
and must themselves be tested.

At minimum consider:

• clean environment
• first installation
• already installed environment
• partially installed environment
• interrupted installation
• failed installation
• rerun after failure
• invalid configuration
• missing optional component
• missing external prerequisite
• restart after installation
• upgrade from previous project state

A successful installer run is not sufficient evidence that the installed
application is correct.

The installed environment must be validated separately from installer success.

============================================================
117. DOCUMENTATION IS PART OF EVERY DEVELOPMENT PHASE
============================================================

Documentation is not a final phase.

For every development phase determine which documentation must exist or change.
Documentation must be updated during the phase in which the corresponding
knowledge is created.

The following events require documentation impact review:

• implementation change
• bug discovery
• bug correction
• new variable or configuration value
• dependency addition or removal
• version change
• architecture change
• audit result
• test discovery
• test failure
• known limitation
• rejected approach that materially informs future work
• installation change
• environment change
• recovery change
• decision change

If a change creates new project knowledge and that knowledge is not represented
in the relevant documentation, the development cycle is not closed.

Documentation required by a phase must be completed within that phase.

Do not defer required documentation to a later phase merely to report the code
as finished.

============================================================
118. WORKLOG AS THE HANDOFF ENTRY POINT
============================================================

The WORKLOG is the operational entry point for another agent.

The WORKLOG is not an independent Source Of Truth for architecture or product
requirements. It is the current navigation and state record that points to the
authoritative sources and records the state of the engineering process.


Its beginning must provide a reading order, not merely a chronological diary.

The reading order must identify:

1. the governing operating framework
2. the project Source Of Truth
3. the current architecture documentation
4. the current implementation state
5. the active decision records
6. the current dependency and environment definition
7. the relevant tests and latest results
8. known limitations and unresolved issues
9. the current development phase
10. the exact next action

Historical material must be clearly distinguishable from current truth.

A new agent should not have to read every historical entry before it can safely
understand the current state.

At minimum the current WORKLOG state should expose: 

• project identity
• current phase
• current objective
• current Source Of Truth
• current implementation state
• current dependency and environment state
• changes made in the current cycle
• reasons for those changes
• documentation updated in the current cycle
• tests and validation performed
• skipped tests and their reasons
• failures and unresolved issues
• current decisions and rejected alternatives where material
• recovery or rollback information
• exact next action

The WORKLOG should maintain the latest usable state at the top or at a clearly
identified current state location. Historical entries may follow below.

============================================================
119. EVERY DEVELOPMENT CYCLE PRODUCES A PACKAGE
============================================================

Every development cycle must produce a recoverable artifact.

This rule applies even when the cycle is very small.

Examples include:

• one file bug fix
• one configuration correction
• one new variable
• one test correction
• one dependency update
• one documentation correction caused by implementation
• one installer correction
• one audit driven change
• one small refactor

The minimum closure sequence is:

IMPLEMENT
→ UPDATE DOCUMENTATION
→ VERIFY ARTIFACTS
→ TEST
→ REVIEW
→ UPDATE STATE
→ UPDATE WORKLOG
→ PACKAGE
→ VERIFY PACKAGE
→ HANDOFF READY

Do not classify a development cycle as closed merely because the code change is
small.

============================================================
120. SINGLE ZIP HANDOFF ARTIFACT
============================================================

The completed development artifact must be delivered as a single ZIP archive.

The ZIP is the primary handoff and recovery package for that development cycle.

Where applicable it must contain:

• changed source files
• required project files
• configuration
• dependency manifests
• lock information
• installer or bootstrap files
• tests
• relevant test results
• current documentation
• WORKLOG
• current state information
• decision information
• audit results
• known limitations
• recovery information
• exact revision information
• artifact metadata
• checksums where appropriate

Do not include secrets.

Do not include irrelevant temporary files merely to make the ZIP appear
complete.

The package should contain everything another agent needs to continue the work
without relying on the original conversation.

============================================================
121. HANDOFF TEST BEFORE CLOSURE
============================================================

Before declaring a cycle complete, mentally remove the current conversation
from the environment.

Assume another agent receives only the ZIP.

That agent must be able to determine:

• what project this is
• what the current state is
• what changed
• why it changed
• what was not changed
• which documents are authoritative
• what dependencies and versions are required
• how the environment is reconstructed
• how installation is performed
• how the project is tested
• what tests actually ran
• which tests were skipped and why
• what failed
• what remains unresolved
• which decisions are current
• what the next action is

If the answer to an important item depends on the previous conversation, the
handoff package is incomplete.

============================================================
122. PACKAGE INTEGRITY AND EXACT STATE
============================================================

The ZIP must represent the same state that was validated.

The minimum evidence relationship is:

CURRENT SOURCE STATE
→ DOCUMENTED STATE
→ TESTED STATE
→ PACKAGED STATE
→ VERIFIED ZIP

Do not make additional undocumented changes after the final test and then
package a different state.

Where practical record:

• project revision
• package name
• package creation time
• package checksum
• validation result

The package itself must be inspected after creation.

Do not report a package as complete merely because the archive command succeeded.
Verify that expected files are present and prohibited files are absent.

============================================================
123. DEPENDENCY PROVENANCE AND LOCKED STATE
============================================================

The project should preserve sufficient information to identify where each
significant dependency came from and exactly which version was validated.

Where the technology supports it, use lock files or equivalent mechanisms to
capture the resolved dependency tree.

When a dependency is updated, the change must record:

• previous state
• new state
• reason for update
• source
• validation performed
• compatibility result
• security result where relevant
• effect on the package

Do not allow a fresh installation to silently resolve a different dependency
state merely because an unpinned range now resolves differently.

============================================================
124. INTERNET FRESHNESS DOES NOT OVERRIDE ENGINEERING JUDGEMENT
============================================================

Fresh external information is required for changing facts.

However, the internet is an evidence source, not the project Source Of Truth.

When evaluating a new version or dependency, distinguish:

LATEST AVAILABLE
from
LATEST APPROPRIATE
from
CURRENTLY VALIDATED

A newer version may be rejected when compatibility, stability, supportability,
licensing, security, reproducibility or project constraints make it unsuitable.

The rejection reason must be recorded when material.

============================================================
125. NO AMBIENT DEPENDENCY RULE
============================================================

A project must not depend silently on ambient machine state.

Examples include:

• a package installed globally
• a runtime version selected by an unrelated user setting
• an undeclared system path
• a local configuration file outside the project
• a manually edited environment variable
• a locally cached model or asset
• an undocumented service already running

If ambient state is genuinely required, it must be identified, versioned or
qualified, validated and documented.

The goal is not to eliminate every host requirement.
The goal is to eliminate hidden host requirements.

============================================================
126. DEVELOPMENT CLOSURE STATES
============================================================

In addition to the project completion states already defined in this document,
every development cycle must report an explicit package state.

Use one of:

PACKAGE READY
PACKAGE READY WITH KNOWN LIMITATION
PACKAGE BLOCKED
PACKAGE DEFERRED BY DECISION
PACKAGE UNVERIFIED

A source change without a verified handoff package must not be reported as a
fully closed development cycle.

============================================================
127. CORE TERMINOLOGY
============================================================

Use the following terms consistently throughout this document and project
process.

SOURCE OF TRUTH

The source currently authoritative for a defined area of project reality.

PROJECT STATE

The explicit, current state of the project, including implementation status,
decisions, constraints, validation state and known limitations.

WORKLOG

The operational navigation and state record that tells a current or receiving
agent what to read, what is current, what changed and what must happen next.

CHECKPOINT

A recoverable record of a known project state that can be independently restored
or inspected.

ARTIFACT

A concrete project output that corresponds to an identifiable project state and
can be inspected independently.

HANDOFF PACKAGE

The complete artifact package required for another agent to continue work without
the original conversation.

CURRENT STATE

The most recent state established and verified as relevant to the present work.

HISTORICAL STATE

A previous state that may provide evidence or reasoning but is not automatically
current truth.

EXECUTION DEPTH

The amount of process, validation and review required for a cycle according to
its risk and blast radius. The levels are MINIMAL, STANDARD and DEEP.

CLOSURE STATE

The explicit state reported at the end of a development cycle, including whether
work is complete, limited, blocked, deferred, accepted as risk or unverified.

PACKAGE STATE

The explicit state of the recoverable handoff artifact for the cycle.

Do not casually replace these terms with synonyms when doing so could create a
second meaning.

============================================================
128. FRAMEWORK GOVERNANCE
============================================================

This document is itself an engineered artifact and must follow the engineering
principles it defines.

A change to this framework must:

• have an explicit reason
• identify affected sections
• identify superseded rules where applicable
• preserve terminology consistency
• preserve the single master lifecycle
• verify that no contradictory execution model was introduced
• remove obsolete or duplicated rules where appropriate
• verify that examples remain clearly illustrative or general
• update the document version
• update the change history
• perform an internal consistency review
• record unresolved governance questions instead of hiding them

A framework update must not silently redefine an existing rule.

If two framework sections conflict, do not select whichever rule is easier to
implement. Surface the contradiction, determine which rule is current, update the
document coherently and record the superseded rule when the distinction matters.

Framework changes require a closure state just like project changes.

============================================================
129. FRAMEWORK CHANGE HISTORY
============================================================

v3.0

Purpose of change:
Audit driven structural correction of v2.2.

Major changes:

• clarified authority between framework and project Source Of Truth
• added explicit behavior when human decision is required but unavailable
• replaced competing development loops with one canonical lifecycle
• introduced MINIMAL, STANDARD and DEEP execution depth
• added controlled terminology
• added framework self governance
• removed unresolved external citation artifacts
• generalized technology specific rendering material
• generalized domain specific semantic examples
• preserved mandatory recovery and handoff closure for every development cycle

Previous versions remain historical evidence and must not be treated as the
current framework unless explicitly designated as such by the project.

Before publishing a new framework version, verify:

• no unresolved tool or citation artifacts remain
• no competing master lifecycle exists
• authority precedence is explicit
• terminology is internally consistent
• examples are clearly illustrative or technology independent
• version and change history are updated
• the final document is the exact state that was reviewed

============================================================
130. OPERATIONAL ABSOLUTE RULE
============================================================

Before changing the project:

READ THIS DOCUMENT IN FULL.
READ THE PROJECT STATE.
READ THE DOCUMENTATION ORDER.
AUDIT THE CURRENT ENVIRONMENT.
AUDIT DEPENDENCIES AND TOOLS USING FRESH SOURCES.
ESTABLISH OR VERIFY THE CANONICAL ENVIRONMENT.
ESTABLISH THE INSTALLATION PLAN.
IDENTIFY REQUIRED DOCUMENTATION UPDATES.
ESTABLISH THE APPROPRIATE EXECUTION DEPTH.
FOLLOW THE MASTER DEVELOPMENT LIFECYCLE.

During the change:

PRESERVE SOURCE OF TRUTH.
KEEP DOCUMENTATION CURRENT.
KEEP THE WORKLOG CURRENT.
DO NOT INVENT MISSING ARCHITECTURE.
VALIDATE THE REAL ARTIFACT.
DO NOT SILENTLY CROSS AUTHORITY BOUNDARIES.

Before closure:

TEST.
REVIEW.
UPDATE STATE.
PACKAGE EVERYTHING REQUIRED.
CREATE ONE ZIP HANDOFF ARTIFACT.
VERIFY THE ZIP.
RUN THE HANDOFF TEST.
REPORT THE TRUE CLOSURE STATE.

A development cycle is closed only when the engineering evidence, documented
state and recoverable handoff artifact agree.

