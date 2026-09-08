SDK Reference & Documentation

Important — Read Before Developing

The SDK included in this repository is an authoritative technical reference for this project.

Before writing, modifying, debugging, or reviewing any code related to the SDK, Mach3 interface, plugin API, motion control interface, or communication with the CNC controller:

1. Read and inspect the entire SDK documentation.
2. Read all relevant header files, source files, API definitions, structures, enumerations, constants, and examples.
3. Understand the purpose and relationship of the SDK's APIs before using them.
4. Do not assume that an API, function, structure, callback, or parameter works in a particular way without verifying it from the SDK documentation or source code.
5. Prefer the SDK's official examples and documented usage patterns over assumptions or generic programming knowledge.
6. When implementing an SDK-related feature, identify the exact SDK API(s) being used and verify their parameters, return values, data types, limitations, and expected calling sequence.
7. Pay particular attention to:
   - API functions
   - callbacks
   - data structures
   - enumerations
   - constants
   - initialization and shutdown procedures
   - threading requirements
   - timing requirements
   - error handling
   - memory ownership
   - communication mechanisms
   - example implementations
   - version-specific behavior

Source of Truth

For SDK-related questions and implementation decisions, use the SDK contained in this repository as the primary source of truth.

If the SDK documentation conflicts with general knowledge or assumptions, follow the SDK documentation.

If the SDK documentation is incomplete or ambiguous:

- Do not invent behavior.
- Clearly state what is known and what is uncertain.
- Inspect the SDK source code, headers, and examples for additional evidence.
- If the behavior still cannot be determined, explicitly identify the uncertainty before implementing it.

Before Implementation

Before implementing a feature that depends on the SDK, first determine:

- Which SDK component/API is relevant?
- Which header or documentation defines it?
- What inputs does it require?
- What outputs does it produce?
- What callbacks or events are involved?
- What initialization is required?
- What cleanup is required?
- Are there timing, threading, or synchronization requirements?
- Are there version-specific limitations?
- Is there an official example demonstrating its use?

Do Not Guess

Never create fictional SDK functions, structures, constants, callbacks, parameters, or behavior.

For example, do not assume that a function such as:

SomeSDKFunction(...)

exists unless it can be verified in the SDK documentation, headers, source code, or examples.

SDK Usage in This Project

The SDK should be treated as a dependency/reference layer rather than as application firmware.

The project's own implementation should clearly separate:

1. SDK-provided functionality
2. Project-specific application logic
3. MCU firmware
4. Hardware-specific interfaces

When referencing an SDK API in project documentation or code comments, use its exact official name.

Recommended Reading Order

When first analyzing this repository:

1. Read this README.
2. Identify the SDK directory.
3. Read the SDK overview/documentation.
4. Inspect the API/header definitions.
5. Inspect the official examples.
6. Identify the APIs relevant to this project.
7. Only then begin implementing or modifying SDK-dependent code.

Documentation of Findings

After analyzing the SDK, maintain a concise project-specific summary of the APIs actually used by this project.

For each API, document:

- API name
- Purpose
- Parameters
- Return value
- Important constraints
- Where it is used in this project
- Relevant SDK documentation/example

Do not copy the entire SDK documentation into the project summary. The summary should explain how the SDK is used in this specific project.
