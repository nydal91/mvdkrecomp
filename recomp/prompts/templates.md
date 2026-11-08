---
name: prompt-templates
description: Instruction templates for Opus 5.5 during recompilation.
---

# Opus Recompilation Templates (High-Reasoning)

Unlike Flash models, **Opus 5.5** excels at deep reasoning, architectural design, and complex logic reconstruction. Use these templates to leverage its high-level intelligence for complex tasks.

## 1. Deep Logic Reconstruction (Assembly $\rightarrow$ Documented C++)
**File:** `recompilation/prompts/decompile_func.txt`
**Use Case:** When you have a complex block of assembly where simple instruction-to-line translation is insufficient.

**Prompt Content:**
```text
You are an elite reverse engineer and software architect. Your task is to perform a high-fidelity static recompilation of the following ARM64 assembly into clean, production-ready C++20.

### Objective:
Reconstruct the high-level logic, intent, and data flow of the original code. Do not merely translate instructions; reconstruct the programmer's intent.

### Requirements:
- **Code Quality:** Write idiomatic, modern C++20. Use `std::` types, `std::optional`, `std::span`, and modern memory management where appropriate.
- **Documentation:** Provide detailed comments explaining the mapping between complex assembly sequences (e.g., indirect jumps, stack manipulations) and your C++ logic.
- **Data Types:** Infer the most appropriate types for registers and memory offsets (e.g., `uint32_t`, `float`, or custom class pointers).
- **Architecture:** Ensure the code is structured logically, using classes/structs if the assembly implies an object-oriented pattern (e.g., via vtable usage).
- **Mobile Optimization:** Assume the target is an Android mobile device; prioritize performance and memory efficiency.

### Assembly Input:
<INSERT_ASSEMBLY_HERE>

### Reconstructed C++:
```

## 2. Object & Data Model Reconstruction (Binary $\rightarrow$ C++ Architecture)
**File:** `recompilation/prompts/reconstruct_struct.txt`
**Use Case:** When you have a large memory dump or a vtable and want to reconstruct the entire class hierarchy.

**Prompt Content:**
```text
You are an expert in binary forensics and software architecture. I will provide a hex dump and associated memory addresses from a Nintendo Switch game.

### Objective:
Reconstruct the C++ class definition and its associated memory layout.

### Requirements:
- **Structure:** Identify members, their types, and their offsets.
- **Polymorphism:** If vtables or RTTI patterns are detected, reconstruct the class inheritance hierarchy.
- **Alignment:** Precisely account for ARM64 memory alignment and padding.
- **Advanced Types:** Identify and name complex types like `glm::vec3`, `Quaternion`, or custom game engine containers.
- **Output:** Provide a complete, compilable C++ header file (`.hpp`) including the necessary includes.

### Hex Dump/Memory Info:
<INSERT_HEX_DUMP_HERE>

### Reconstructed Header:
```

## 3. Intelligent Asset Evolution (Custom $\rightarrow$ Optimized Schema)
**File:** `recompilation/prompts/translate_asset.txt`
**Use Case:** When you have raw asset data (like `.byml`) and want to move from a "file format" to an "engine-optimized" format.

**Prompt Content:**
```text
You are a game engine specialist. I will provide raw data from a Nintendo Switch asset file.

### Objective:
Translate this data into an optimized, high-performance data structure for a modern Android game engine (e.g., Unreal, Unity, or a custom C++ engine).

### Requirements:
- **Format Choice:** Transform the data into <CHOOSE_ONE: Optimized C++ Binary Structs / JSON / Protobuf / XML>.
- **Data Normalization:** Clean up any proprietary Switch-specific encoding or compression.
- **Engine Integration:** Structure the data so it is ready for direct loading into a game engine's component system (ECS).
- **Detail:** Retain all metadata, but optimize the layout for cache-locality where possible.

### Asset Data:
<INSERT_ASSET_CONTENT_HERE>

### Transformed Output:
```
