"""
DRX AI Code Generator
Generates C++/Python code from natural language using Claude API
"""

import logging
from typing import Dict, Any, Optional
from dataclasses import dataclass

try:
    import anthropic
    ANTHROPIC_AVAILABLE = True
except ImportError:
    ANTHROPIC_AVAILABLE = False


@dataclass
class CodeGenerationResult:
    """Result of code generation"""
    success: bool
    code: str
    language: str
    iterations: int
    error: Optional[str] = None


class CodeGenerator:
    """
    Generates code using Claude API.

    Handles:
    - C++ engine code
    - Python scripts
    - GLSL shaders
    - Configuration files
    """

    def __init__(self, config: Dict[str, Any]):
        """
        Initialize the code generator.

        Args:
            config: Configuration with anthropic_api_key, model, etc.
        """
        self.config = config
        self.logger = logging.getLogger("DRX.AI.CodeGen")

        # Initialize Anthropic client if available
        self.client = None
        if ANTHROPIC_AVAILABLE:
            api_key = config.get("anthropic_api_key")
            if api_key:
                self.client = anthropic.Anthropic(api_key=api_key)

        self.model = config.get("model", "claude-sonnet-4-20250514")
        self.max_iterations = config.get("max_iterations", 3)

        self.logger.info("Code generator initialized")

    async def generate(self, context: Dict[str, Any]) -> CodeGenerationResult:
        """
        Generate code from context.

        Args:
            context: Full context including prompt, APIs, examples, etc.

        Returns:
            CodeGenerationResult
        """
        prompt = context.get("prompt", "")
        language = context.get("language", "cpp")

        self.logger.info(f"Generating {language} code")

        # Build comprehensive prompt
        full_prompt = self._build_prompt(context)

        # Generate with retries
        for iteration in range(self.max_iterations):
            try:
                self.logger.debug(f"Generation attempt {iteration + 1}")

                code = await self._call_api(full_prompt)

                if code:
                    # Extract code from response
                    extracted = self._extract_code(code, language)

                    if extracted:
                        return CodeGenerationResult(
                            success=True,
                            code=extracted,
                            language=language,
                            iterations=iteration + 1
                        )

            except Exception as e:
                self.logger.error(f"Generation failed: {e}")
                if iteration == self.max_iterations - 1:
                    return CodeGenerationResult(
                        success=False,
                        code="",
                        language=language,
                        iterations=iteration + 1,
                        error=str(e)
                    )

        return CodeGenerationResult(
            success=False,
            code="",
            language=language,
            iterations=self.max_iterations,
            error="Max iterations reached"
        )

    async def generate_from_prompt(self, prompt: str) -> CodeGenerationResult:
        """
        Simple generation from just a prompt.

        Args:
            prompt: The prompt to generate from

        Returns:
            CodeGenerationResult
        """
        context = {
            "prompt": prompt,
            "language": self._detect_language(prompt)
        }
        return await self.generate(context)

    async def _call_api(self, prompt: str) -> Optional[str]:
        """
        Call the Anthropic API.

        Args:
            prompt: The full prompt to send

        Returns:
            Response text or None
        """
        if not self.client:
            # Fallback for when API is not available
            self.logger.warning("Anthropic client not available, returning placeholder")
            return self._generate_placeholder()

        try:
            message = self.client.messages.create(
                model=self.model,
                max_tokens=4096,
                temperature=0.3,  # Lower for code precision
                messages=[
                    {
                        "role": "user",
                        "content": prompt
                    }
                ]
            )

            return message.content[0].text

        except Exception as e:
            self.logger.error(f"API call failed: {e}")
            raise

    def _build_prompt(self, context: Dict[str, Any]) -> str:
        """Build comprehensive prompt for code generation."""

        parts = []

        # System context
        language = context.get("language", "cpp")

        if language == "cpp":
            parts.append("""You are an expert C++ game engine developer.
Generate high-quality, production-ready code following these guidelines:

REQUIREMENTS:
- C++17 standard
- Google C++ Style Guide with DRX adaptations
- RAII for resource management
- Smart pointers (no raw owning pointers)
- Const correctness
- Clear, self-documenting names
- Comprehensive error handling
- Performance-conscious

OUTPUT FORMAT:
- Return ONLY the code, no explanations unless requested
- Include necessary headers
- Add brief comments for complex logic
- Use proper DRX namespacing
""")
        elif language == "python":
            parts.append("""You are an expert Python developer for AI systems.
Generate clean, well-documented Python code following:

REQUIREMENTS:
- Python 3.11+ compatible
- Type hints throughout
- Async where appropriate
- Clear docstrings (Google style)
- Proper exception handling

OUTPUT FORMAT:
- Return ONLY the code
- Include all imports
- Add docstrings to functions and classes
""")
        elif language == "glsl":
            parts.append("""You are an expert graphics programmer.
Generate optimized GLSL shader code:

REQUIREMENTS:
- GLSL 4.5+ or Vulkan GLSL
- Efficient math operations
- Proper precision qualifiers
- Clear variable naming

OUTPUT FORMAT:
- Return ONLY the shader code
- Include version directive
- Comment complex calculations
""")

        # User request
        parts.append(f"\nTASK:\n{context['prompt']}\n")

        # Coding guidelines
        if context.get("coding_guidelines"):
            guidelines = context["coding_guidelines"]
            parts.append("\nCODING GUIDELINES:\n")
            for practice in guidelines.get("best_practices", []):
                parts.append(f"- {practice}\n")

        # Constraints
        if context.get("constraints"):
            parts.append("\nCONSTRAINTS:\n")
            for key, value in context["constraints"].items():
                parts.append(f"- {key}: {value}\n")

        return "".join(parts)

    def _extract_code(self, response: str, language: str) -> Optional[str]:
        """Extract code from response."""

        # Language markers
        markers = {
            "cpp": ["```cpp", "```c++", "```"],
            "python": ["```python", "```py", "```"],
            "glsl": ["```glsl", "```"]
        }

        lang_markers = markers.get(language, ["```"])

        for marker in lang_markers:
            if marker in response:
                parts = response.split(marker)
                if len(parts) >= 2:
                    code = parts[1].split("```")[0].strip()
                    return code

        # If no code blocks, return response as-is
        return response.strip()

    def _detect_language(self, prompt: str) -> str:
        """Detect language from prompt."""
        prompt_lower = prompt.lower()

        if any(kw in prompt_lower for kw in ["python", ".py", "async def"]):
            return "python"
        if any(kw in prompt_lower for kw in ["shader", "glsl", "vertex", "fragment"]):
            return "glsl"

        return "cpp"

    def _generate_placeholder(self) -> str:
        """Generate placeholder code when API is not available."""
        return """// DRX Engine - Generated Code Placeholder
// API not available - this is placeholder code

#include "Core/Public/CoreTypes.h"

namespace DRX {

class GeneratedClass {
public:
    GeneratedClass() = default;
    ~GeneratedClass() = default;

    void Initialize() {
        // TODO: Implement
    }

    void Update(float delta_time) {
        // TODO: Implement
    }

    void Shutdown() {
        // TODO: Implement
    }
};

} // namespace DRX
"""
