"""
DRX Engine - Advanced AI Code Generation System
Autonomous code generation with Claude API integration
"""

import os
import json
import asyncio
import hashlib
from typing import Dict, List, Optional, Any, Callable
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
import re

class GenerationTarget(Enum):
    """Target language/system for code generation"""
    CPP_ENGINE = "cpp_engine"
    CPP_GAME = "cpp_game"
    GLSL_SHADER = "glsl_shader"
    LUA_SCRIPT = "lua_script"
    SCENE_JSON = "scene_json"
    BLUEPRINT = "blueprint"

class GenerationPriority(Enum):
    """Priority for generation tasks"""
    CRITICAL = 0
    HIGH = 1
    NORMAL = 2
    LOW = 3
    BACKGROUND = 4

@dataclass
class CodeTemplate:
    """Template for code generation"""
    name: str
    target: GenerationTarget
    template: str
    parameters: Dict[str, Any] = field(default_factory=dict)
    dependencies: List[str] = field(default_factory=list)

@dataclass
class GenerationContext:
    """Context for code generation"""
    project_root: str
    target: GenerationTarget
    existing_code: Dict[str, str] = field(default_factory=dict)
    dependencies: List[str] = field(default_factory=list)
    style_guide: Optional[str] = None
    constraints: List[str] = field(default_factory=list)

@dataclass
class GeneratedCode:
    """Result of code generation"""
    filepath: str
    content: str
    target: GenerationTarget
    checksum: str
    metadata: Dict[str, Any] = field(default_factory=dict)

    def compute_checksum(self) -> str:
        return hashlib.sha256(self.content.encode()).hexdigest()[:16]

class CodeAnalyzer:
    """Analyzes existing code for context"""

    def __init__(self, project_root: str):
        self.project_root = Path(project_root)
        self.cache: Dict[str, Any] = {}

    def analyze_cpp_file(self, filepath: str) -> Dict[str, Any]:
        """Analyze a C++ file for classes, functions, includes"""
        content = Path(filepath).read_text()

        analysis = {
            "includes": self._extract_includes(content),
            "namespaces": self._extract_namespaces(content),
            "classes": self._extract_classes(content),
            "functions": self._extract_functions(content),
            "dependencies": []
        }

        return analysis

    def _extract_includes(self, content: str) -> List[str]:
        pattern = r'#include\s*[<"]([^>"]+)[>"]'
        return re.findall(pattern, content)

    def _extract_namespaces(self, content: str) -> List[str]:
        pattern = r'namespace\s+(\w+(?:::\w+)*)'
        return re.findall(pattern, content)

    def _extract_classes(self, content: str) -> List[Dict[str, Any]]:
        classes = []
        pattern = r'class\s+(\w+)(?:\s*:\s*(?:public|protected|private)\s+(\w+))?'
        for match in re.finditer(pattern, content):
            classes.append({
                "name": match.group(1),
                "base": match.group(2) if match.group(2) else None
            })
        return classes

    def _extract_functions(self, content: str) -> List[Dict[str, Any]]:
        functions = []
        # Simplified function extraction
        pattern = r'(?:virtual\s+)?(\w+(?:<[^>]+>)?(?:\s*\*)?)\s+(\w+)\s*\([^)]*\)'
        for match in re.finditer(pattern, content):
            functions.append({
                "return_type": match.group(1),
                "name": match.group(2)
            })
        return functions

    def get_project_structure(self) -> Dict[str, Any]:
        """Get overall project structure"""
        structure = {
            "engine_modules": [],
            "game_modules": [],
            "shaders": [],
            "scripts": [],
            "assets": []
        }

        for path in self.project_root.rglob("*"):
            if path.is_file():
                rel_path = str(path.relative_to(self.project_root))

                if "Engine/Source" in rel_path:
                    structure["engine_modules"].append(rel_path)
                elif rel_path.endswith((".glsl", ".vert", ".frag")):
                    structure["shaders"].append(rel_path)
                elif rel_path.endswith(".lua"):
                    structure["scripts"].append(rel_path)

        return structure

class PromptBuilder:
    """Builds prompts for Claude API"""

    SYSTEM_PROMPT = """You are an expert game engine developer working on the DRX Engine.
You write high-quality, efficient, and well-documented code following these guidelines:

C++ Guidelines:
- Use C++17 features appropriately
- Follow RAII for resource management
- Use smart pointers (SharedPtr, UniquePtr) instead of raw pointers
- Prefer composition over inheritance
- Use DRX namespace and proper module namespaces
- Include proper error handling with Result<T> type
- Add documentation comments for public APIs

GLSL Guidelines:
- Target Vulkan GLSL (version 450)
- Use proper layout qualifiers
- Optimize for GPU parallelism
- Include comments explaining shader logic

Lua Guidelines:
- Use local variables when possible
- Follow DRX scripting conventions
- Implement proper lifecycle methods (OnCreate, OnUpdate, etc.)

Always respond with complete, compilable code. Never use placeholders or TODO comments.
"""

    def __init__(self):
        self.templates: Dict[str, CodeTemplate] = {}
        self._load_default_templates()

    def _load_default_templates(self):
        """Load default code templates"""

        # Component template
        self.templates["component"] = CodeTemplate(
            name="Component",
            target=GenerationTarget.CPP_ENGINE,
            template="""
#pragma once

#include "Core/Public/CoreTypes.h"
#include "Scene/Public/Entity.h"

namespace DRX::{{module}} {

/**
 * {{description}}
 */
class {{class_name}} {
public:
    {{class_name}}() = default;
    ~{{class_name}}() = default;

    // Lifecycle
    void OnCreate();
    void OnDestroy();
    void OnUpdate(float32 delta_time);

    // Properties
{{properties}}

private:
{{private_members}}
};

} // namespace DRX::{{module}}
""",
            parameters={"module": "", "class_name": "", "description": "", "properties": "", "private_members": ""}
        )

        # System template
        self.templates["system"] = CodeTemplate(
            name="System",
            target=GenerationTarget.CPP_ENGINE,
            template="""
#pragma once

#include "Core/Public/CoreTypes.h"

namespace DRX::{{module}} {

/**
 * {{description}}
 */
class {{class_name}} {
public:
    static {{class_name}}& Instance();

    bool Initialize();
    void Shutdown();
    void Update(float32 delta_time);

{{public_methods}}

private:
    {{class_name}}() = default;
    ~{{class_name}}() = default;
    {{class_name}}(const {{class_name}}&) = delete;
    {{class_name}}& operator=(const {{class_name}}&) = delete;

{{private_members}}

    bool initialized_ = false;
};

} // namespace DRX::{{module}}
""",
            parameters={"module": "", "class_name": "", "description": "", "public_methods": "", "private_members": ""}
        )

        # Shader template
        self.templates["shader"] = CodeTemplate(
            name="Shader",
            target=GenerationTarget.GLSL_SHADER,
            template="""
/**
 * DRX Engine - {{shader_name}}
 * {{description}}
 */
#version 450

{{inputs}}

{{outputs}}

{{uniforms}}

{{functions}}

void main() {
{{main_body}}
}
""",
            parameters={"shader_name": "", "description": "", "inputs": "", "outputs": "", "uniforms": "", "functions": "", "main_body": ""}
        )

        # Lua script template
        self.templates["lua_script"] = CodeTemplate(
            name="LuaScript",
            target=GenerationTarget.LUA_SCRIPT,
            template="""
--[[
    DRX Engine - {{script_name}}
    {{description}}
]]

local {{script_name}} = {}

function {{script_name}}:OnCreate()
{{on_create}}
end

function {{script_name}}:OnDestroy()
{{on_destroy}}
end

function {{script_name}}:OnUpdate(deltaTime)
{{on_update}}
end

{{additional_methods}}

return {{script_name}}
""",
            parameters={"script_name": "", "description": "", "on_create": "", "on_destroy": "", "on_update": "", "additional_methods": ""}
        )

    def build_generation_prompt(self, request: str, context: GenerationContext) -> str:
        """Build a prompt for code generation"""

        prompt_parts = [
            f"Generate code for the DRX Engine with the following requirements:\n\n{request}\n"
        ]

        if context.existing_code:
            prompt_parts.append("\nExisting related code for context:")
            for filepath, content in list(context.existing_code.items())[:3]:
                prompt_parts.append(f"\n--- {filepath} ---\n{content[:2000]}")

        if context.dependencies:
            prompt_parts.append(f"\nRequired dependencies: {', '.join(context.dependencies)}")

        if context.constraints:
            prompt_parts.append(f"\nConstraints: {', '.join(context.constraints)}")

        if context.style_guide:
            prompt_parts.append(f"\nStyle guide: {context.style_guide}")

        prompt_parts.append("\n\nProvide complete, compilable code with no placeholders.")

        return "\n".join(prompt_parts)

    def build_refactor_prompt(self, code: str, instructions: str) -> str:
        """Build a prompt for code refactoring"""
        return f"""Refactor the following code according to these instructions:

Instructions: {instructions}

Original Code:
```
{code}
```

Provide the complete refactored code.
"""

    def build_fix_prompt(self, code: str, error: str) -> str:
        """Build a prompt for fixing code errors"""
        return f"""Fix the following error in this code:

Error: {error}

Code:
```
{code}
```

Provide the complete fixed code with an explanation of the fix.
"""

class CodeGenerator:
    """Main code generation engine"""

    def __init__(self, api_key: str, project_root: str):
        self.api_key = api_key
        self.project_root = Path(project_root)
        self.analyzer = CodeAnalyzer(project_root)
        self.prompt_builder = PromptBuilder()
        self.generation_history: List[GeneratedCode] = []
        self.validators: Dict[GenerationTarget, Callable] = {}

        self._setup_validators()

    def _setup_validators(self):
        """Setup code validators for each target"""
        self.validators[GenerationTarget.CPP_ENGINE] = self._validate_cpp
        self.validators[GenerationTarget.CPP_GAME] = self._validate_cpp
        self.validators[GenerationTarget.GLSL_SHADER] = self._validate_glsl
        self.validators[GenerationTarget.LUA_SCRIPT] = self._validate_lua

    def _validate_cpp(self, code: str) -> tuple[bool, List[str]]:
        """Validate C++ code syntax"""
        errors = []

        # Check for basic syntax
        if code.count('{') != code.count('}'):
            errors.append("Mismatched braces")
        if code.count('(') != code.count(')'):
            errors.append("Mismatched parentheses")

        # Check for required includes
        if "class " in code and "#pragma once" not in code and "#ifndef" not in code:
            errors.append("Header file missing include guard")

        return len(errors) == 0, errors

    def _validate_glsl(self, code: str) -> tuple[bool, List[str]]:
        """Validate GLSL code"""
        errors = []

        if "#version" not in code:
            errors.append("Missing #version directive")
        if "void main()" not in code and "void main(void)" not in code:
            errors.append("Missing main function")

        return len(errors) == 0, errors

    def _validate_lua(self, code: str) -> tuple[bool, List[str]]:
        """Validate Lua code"""
        errors = []

        # Check for common Lua syntax errors
        if code.count("function") > code.count("end"):
            errors.append("Missing 'end' for function")

        return len(errors) == 0, errors

    async def generate(self, request: str, target: GenerationTarget,
                       output_path: Optional[str] = None) -> GeneratedCode:
        """Generate code from a natural language request"""

        # Build context
        context = GenerationContext(
            project_root=str(self.project_root),
            target=target
        )

        # Get related existing code
        if target in [GenerationTarget.CPP_ENGINE, GenerationTarget.CPP_GAME]:
            context.existing_code = self._get_related_cpp_code(request)

        # Build prompt
        prompt = self.prompt_builder.build_generation_prompt(request, context)

        # Call Claude API
        generated_content = await self._call_claude_api(prompt)

        # Extract code from response
        code = self._extract_code(generated_content, target)

        # Validate
        validator = self.validators.get(target)
        if validator:
            valid, errors = validator(code)
            if not valid:
                # Try to fix errors
                code = await self._fix_code(code, errors, target)

        # Create result
        result = GeneratedCode(
            filepath=output_path or self._generate_filepath(request, target),
            content=code,
            target=target,
            checksum="",
            metadata={"request": request}
        )
        result.checksum = result.compute_checksum()

        # Save if path provided
        if output_path:
            self._save_code(result)

        self.generation_history.append(result)
        return result

    async def generate_component(self, name: str, description: str,
                                  properties: List[Dict[str, str]]) -> GeneratedCode:
        """Generate a component class"""

        props_str = "\n".join([
            f"    void Set{p['name'].title()}({p['type']} value);\n    {p['type']} Get{p['name'].title()}() const;"
            for p in properties
        ])

        members_str = "\n".join([
            f"    {p['type']} {p['name']}_;"
            for p in properties
        ])

        request = f"Create a component called {name}: {description}"
        return await self.generate(request, GenerationTarget.CPP_ENGINE)

    async def generate_system(self, name: str, description: str,
                               methods: List[Dict[str, Any]]) -> GeneratedCode:
        """Generate a system class"""
        request = f"Create a singleton system called {name}: {description}"
        return await self.generate(request, GenerationTarget.CPP_ENGINE)

    async def generate_shader(self, name: str, shader_type: str,
                               description: str) -> GeneratedCode:
        """Generate a shader"""
        request = f"Create a {shader_type} shader called {name}: {description}"
        return await self.generate(request, GenerationTarget.GLSL_SHADER)

    async def generate_script(self, name: str, description: str,
                               behaviors: List[str]) -> GeneratedCode:
        """Generate a Lua script"""
        behaviors_str = ", ".join(behaviors)
        request = f"Create a Lua script called {name}: {description}. Behaviors: {behaviors_str}"
        return await self.generate(request, GenerationTarget.LUA_SCRIPT)

    async def refactor(self, filepath: str, instructions: str) -> GeneratedCode:
        """Refactor existing code"""
        original_code = Path(filepath).read_text()
        prompt = self.prompt_builder.build_refactor_prompt(original_code, instructions)

        refactored = await self._call_claude_api(prompt)
        code = self._extract_code(refactored, self._detect_target(filepath))

        result = GeneratedCode(
            filepath=filepath,
            content=code,
            target=self._detect_target(filepath),
            checksum="",
            metadata={"original_checksum": hashlib.sha256(original_code.encode()).hexdigest()[:16]}
        )
        result.checksum = result.compute_checksum()

        return result

    async def _call_claude_api(self, prompt: str) -> str:
        """Call Claude API for code generation"""
        try:
            import anthropic

            client = anthropic.Anthropic(api_key=self.api_key)

            message = client.messages.create(
                model="claude-sonnet-4-20250514",
                max_tokens=8192,
                system=PromptBuilder.SYSTEM_PROMPT,
                messages=[{"role": "user", "content": prompt}]
            )

            return message.content[0].text

        except ImportError:
            # Fallback for when anthropic isn't installed
            return self._mock_generation(prompt)
        except Exception as e:
            raise RuntimeError(f"Claude API error: {e}")

    def _mock_generation(self, prompt: str) -> str:
        """Mock generation for testing without API"""
        return "// Generated code placeholder\n// Install anthropic package for actual generation"

    async def _fix_code(self, code: str, errors: List[str], target: GenerationTarget) -> str:
        """Attempt to fix code errors"""
        error_str = "; ".join(errors)
        prompt = self.prompt_builder.build_fix_prompt(code, error_str)
        fixed = await self._call_claude_api(prompt)
        return self._extract_code(fixed, target)

    def _extract_code(self, response: str, target: GenerationTarget) -> str:
        """Extract code from API response"""
        # Look for code blocks
        patterns = [
            r'```(?:cpp|c\+\+|glsl|lua|json)?\n(.*?)```',
            r'```\n(.*?)```'
        ]

        for pattern in patterns:
            matches = re.findall(pattern, response, re.DOTALL)
            if matches:
                return matches[0].strip()

        # Return as-is if no code blocks found
        return response.strip()

    def _get_related_cpp_code(self, request: str) -> Dict[str, str]:
        """Get related C++ code for context"""
        related = {}

        # Search for relevant files based on keywords in request
        keywords = request.lower().split()

        for cpp_file in self.project_root.rglob("*.h"):
            filename = cpp_file.stem.lower()
            if any(kw in filename for kw in keywords):
                try:
                    related[str(cpp_file)] = cpp_file.read_text()
                except:
                    pass

            if len(related) >= 3:
                break

        return related

    def _detect_target(self, filepath: str) -> GenerationTarget:
        """Detect generation target from filepath"""
        ext = Path(filepath).suffix.lower()

        if ext in ['.h', '.hpp', '.cpp', '.cc']:
            if 'Engine' in filepath:
                return GenerationTarget.CPP_ENGINE
            return GenerationTarget.CPP_GAME
        elif ext in ['.glsl', '.vert', '.frag']:
            return GenerationTarget.GLSL_SHADER
        elif ext == '.lua':
            return GenerationTarget.LUA_SCRIPT
        elif ext == '.json':
            return GenerationTarget.SCENE_JSON

        return GenerationTarget.CPP_ENGINE

    def _generate_filepath(self, request: str, target: GenerationTarget) -> str:
        """Generate a filepath from request"""
        # Extract potential class/file name from request
        words = request.split()
        name = "Generated"

        for i, word in enumerate(words):
            if word.lower() in ['class', 'component', 'system', 'shader', 'script']:
                if i + 1 < len(words):
                    name = words[i + 1].replace(',', '').replace('.', '')
                    break

        ext_map = {
            GenerationTarget.CPP_ENGINE: '.h',
            GenerationTarget.CPP_GAME: '.h',
            GenerationTarget.GLSL_SHADER: '.glsl',
            GenerationTarget.LUA_SCRIPT: '.lua',
            GenerationTarget.SCENE_JSON: '.json'
        }

        return f"{name}{ext_map.get(target, '.txt')}"

    def _save_code(self, result: GeneratedCode):
        """Save generated code to file"""
        filepath = self.project_root / result.filepath
        filepath.parent.mkdir(parents=True, exist_ok=True)
        filepath.write_text(result.content)


class GameGenerator:
    """High-level game generation from natural language"""

    def __init__(self, code_generator: CodeGenerator):
        self.code_gen = code_generator

    async def generate_fps_game(self, description: str) -> Dict[str, GeneratedCode]:
        """Generate a complete FPS game from description"""
        results = {}

        # Generate player controller
        results['player'] = await self.code_gen.generate(
            f"Create an FPS player controller for: {description}",
            GenerationTarget.CPP_GAME,
            "Game/Source/Player/PlayerController.h"
        )

        # Generate weapon system
        results['weapon'] = await self.code_gen.generate(
            f"Create a weapon system with shooting mechanics for: {description}",
            GenerationTarget.CPP_GAME,
            "Game/Source/Weapons/WeaponSystem.h"
        )

        # Generate enemy AI
        results['enemy'] = await self.code_gen.generate(
            f"Create enemy AI with pathfinding and combat for: {description}",
            GenerationTarget.CPP_GAME,
            "Game/Source/AI/EnemyAI.h"
        )

        # Generate game mode
        results['gamemode'] = await self.code_gen.generate(
            f"Create game mode logic for: {description}",
            GenerationTarget.CPP_GAME,
            "Game/Source/GameMode/GameMode.h"
        )

        # Generate HUD
        results['hud'] = await self.code_gen.generate(
            f"Create HUD/UI for: {description}",
            GenerationTarget.CPP_GAME,
            "Game/Source/UI/GameHUD.h"
        )

        return results

    async def generate_from_prompt(self, prompt: str) -> Dict[str, GeneratedCode]:
        """Generate game elements from a free-form prompt"""

        # Analyze prompt to determine what to generate
        prompt_lower = prompt.lower()
        results = {}

        if any(word in prompt_lower for word in ['player', 'character', 'controller']):
            results['player'] = await self.code_gen.generate(
                f"Player controller: {prompt}",
                GenerationTarget.CPP_GAME
            )

        if any(word in prompt_lower for word in ['enemy', 'ai', 'npc']):
            results['enemy'] = await self.code_gen.generate(
                f"Enemy AI: {prompt}",
                GenerationTarget.CPP_GAME
            )

        if any(word in prompt_lower for word in ['weapon', 'gun', 'shoot']):
            results['weapon'] = await self.code_gen.generate(
                f"Weapon system: {prompt}",
                GenerationTarget.CPP_GAME
            )

        if any(word in prompt_lower for word in ['shader', 'effect', 'visual']):
            results['shader'] = await self.code_gen.generate(
                f"Visual effect shader: {prompt}",
                GenerationTarget.GLSL_SHADER
            )

        if any(word in prompt_lower for word in ['script', 'behavior', 'logic']):
            results['script'] = await self.code_gen.generate(
                f"Game script: {prompt}",
                GenerationTarget.LUA_SCRIPT
            )

        return results
