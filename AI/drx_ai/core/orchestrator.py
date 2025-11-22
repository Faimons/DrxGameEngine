"""
DRX AI Orchestrator
Main entry point for all AI-driven development operations
"""

import asyncio
import logging
from typing import Dict, Any, Optional, List
from dataclasses import dataclass, field
from enum import Enum
from datetime import datetime

from .code_generator import CodeGenerator


class IntentType(Enum):
    """Types of user intents"""
    CODE_GENERATION = "code_generation"
    OPTIMIZATION = "optimization"
    BUG_FIX = "bug_fix"
    FEATURE_REQUEST = "feature_request"
    GAME_GENERATION = "game_generation"
    DOCUMENTATION = "documentation"
    REFACTORING = "refactoring"


@dataclass
class Intent:
    """Analyzed user intent"""
    type: IntentType
    confidence: float
    details: Dict[str, Any] = field(default_factory=dict)


@dataclass
class TaskResult:
    """Result of an AI task"""
    success: bool
    output: Any
    metrics: Dict[str, Any] = field(default_factory=dict)
    error: Optional[str] = None
    duration: float = 0.0


@dataclass
class Interaction:
    """Record of an AI interaction for learning"""
    timestamp: datetime
    prompt: str
    context: Dict[str, Any]
    intent: Intent
    result: TaskResult


class ClaudeCodeOrchestrator:
    """
    Main orchestrator controlling the entire AI development process.

    This class coordinates:
    - Code generation
    - Bug fixing
    - Optimization
    - Game generation
    - Self-learning
    """

    def __init__(self, config: Dict[str, Any]):
        """
        Initialize the orchestrator.

        Args:
            config: Configuration dictionary containing:
                - anthropic_api_key: API key for Claude
                - model: Model to use (default: claude-sonnet-4-20250514)
                - build_dir: Build directory path
                - api_docs_path: Path to API documentation
                - examples_path: Path to code examples
        """
        self.config = config
        self.logger = logging.getLogger("DRX.AI.Orchestrator")

        # Initialize subsystems
        self.code_generator = CodeGenerator(config)

        # Interaction history for learning
        self.interactions: List[Interaction] = []

        self.logger.info("Orchestrator initialized")

    async def process_request(
        self,
        prompt: str,
        context: Optional[Dict[str, Any]] = None
    ) -> TaskResult:
        """
        Main entry point for processing AI requests.

        Args:
            prompt: Natural language request
            context: Additional context (project state, files, etc.)

        Returns:
            TaskResult with outcome
        """
        start_time = asyncio.get_event_loop().time()

        self.logger.info(f"Processing request: {prompt[:100]}...")

        # Analyze intent
        intent = await self._analyze_intent(prompt, context)
        self.logger.info(f"Detected intent: {intent.type.value} (confidence: {intent.confidence:.2f})")

        # Route to appropriate handler
        result = await self._route_request(intent, prompt, context or {})

        result.duration = asyncio.get_event_loop().time() - start_time

        # Record interaction for learning
        interaction = Interaction(
            timestamp=datetime.now(),
            prompt=prompt,
            context=context or {},
            intent=intent,
            result=result
        )
        self.interactions.append(interaction)

        return result

    async def _analyze_intent(
        self,
        prompt: str,
        context: Optional[Dict[str, Any]]
    ) -> Intent:
        """
        Analyze user intent from prompt.

        Uses keyword matching for now. In production, this would use
        a fine-tuned classifier model.
        """
        prompt_lower = prompt.lower()

        # Game generation
        if any(kw in prompt_lower for kw in ["create game", "generate game", "make game", "build game"]):
            return Intent(
                type=IntentType.GAME_GENERATION,
                confidence=0.9,
                details={"genre": self._extract_genre(prompt)}
            )

        # Code generation
        if any(kw in prompt_lower for kw in ["create", "implement", "build", "generate", "add", "write"]):
            return Intent(
                type=IntentType.CODE_GENERATION,
                confidence=0.85,
                details={"language": self._detect_language(prompt, context)}
            )

        # Optimization
        if any(kw in prompt_lower for kw in ["optimize", "improve", "faster", "performance", "speed up"]):
            return Intent(
                type=IntentType.OPTIMIZATION,
                confidence=0.8,
                details={}
            )

        # Bug fixing
        if any(kw in prompt_lower for kw in ["fix", "bug", "error", "crash", "broken", "not working"]):
            return Intent(
                type=IntentType.BUG_FIX,
                confidence=0.85,
                details={}
            )

        # Refactoring
        if any(kw in prompt_lower for kw in ["refactor", "clean up", "restructure", "reorganize"]):
            return Intent(
                type=IntentType.REFACTORING,
                confidence=0.8,
                details={}
            )

        # Documentation
        if any(kw in prompt_lower for kw in ["document", "explain", "comment", "describe"]):
            return Intent(
                type=IntentType.DOCUMENTATION,
                confidence=0.8,
                details={}
            )

        # Default to code generation
        return Intent(
            type=IntentType.CODE_GENERATION,
            confidence=0.5,
            details={}
        )

    async def _route_request(
        self,
        intent: Intent,
        prompt: str,
        context: Dict[str, Any]
    ) -> TaskResult:
        """Route request to appropriate handler based on intent."""

        handlers = {
            IntentType.CODE_GENERATION: self._handle_code_generation,
            IntentType.OPTIMIZATION: self._handle_optimization,
            IntentType.BUG_FIX: self._handle_bug_fix,
            IntentType.GAME_GENERATION: self._handle_game_generation,
            IntentType.REFACTORING: self._handle_refactoring,
            IntentType.DOCUMENTATION: self._handle_documentation,
        }

        handler = handlers.get(intent.type, self._handle_code_generation)
        return await handler(prompt, context, intent)

    async def _handle_code_generation(
        self,
        prompt: str,
        context: Dict[str, Any],
        intent: Intent
    ) -> TaskResult:
        """Handle code generation requests."""
        self.logger.info("Handling code generation")

        # Build comprehensive context
        full_context = self._build_context(prompt, context)

        # Generate code
        result = await self.code_generator.generate(full_context)

        return TaskResult(
            success=result.success,
            output=result.code if result.success else None,
            metrics={
                "lines_of_code": len(result.code.split('\n')) if result.code else 0,
                "iterations": result.iterations,
                "language": result.language
            },
            error=result.error
        )

    async def _handle_optimization(
        self,
        prompt: str,
        context: Dict[str, Any],
        intent: Intent
    ) -> TaskResult:
        """Handle optimization requests."""
        self.logger.info("Handling optimization")

        target_code = context.get("target_code", "")
        if not target_code:
            return TaskResult(
                success=False,
                output=None,
                metrics={},
                error="No target code provided for optimization"
            )

        optimization_prompt = f"""
Optimize the following code for better performance:

```
{target_code}
```

Focus on:
1. Algorithm efficiency
2. Memory usage
3. Cache optimization
4. Reduce allocations
5. Vectorization opportunities

Return the optimized code with comments explaining changes.
"""

        result = await self.code_generator.generate_from_prompt(optimization_prompt)

        return TaskResult(
            success=result.success,
            output=result.code if result.success else None,
            metrics={"optimized": result.success},
            error=result.error
        )

    async def _handle_bug_fix(
        self,
        prompt: str,
        context: Dict[str, Any],
        intent: Intent
    ) -> TaskResult:
        """Handle bug fixing requests."""
        self.logger.info("Handling bug fix")

        buggy_code = context.get("code", "")
        error_msg = context.get("error", "")
        stack_trace = context.get("stack_trace", "")

        fix_prompt = f"""
Fix the bug in this code:

```
{buggy_code}
```

Error: {error_msg}

Stack trace:
{stack_trace}

User description: {prompt}

Provide:
1. Root cause analysis
2. Fixed code
3. Explanation of the fix
"""

        result = await self.code_generator.generate_from_prompt(fix_prompt)

        return TaskResult(
            success=result.success,
            output=result.code if result.success else None,
            metrics={"fix_generated": result.success},
            error=result.error
        )

    async def _handle_game_generation(
        self,
        prompt: str,
        context: Dict[str, Any],
        intent: Intent
    ) -> TaskResult:
        """Handle complete game generation."""
        self.logger.info("Handling game generation")

        genre = intent.details.get("genre", "fps")

        design_prompt = f"""
You are a professional game designer. Create a complete game design for:

Concept: {prompt}
Genre: {genre.upper()}

Provide a JSON game design document including:
1. Core mechanics (movement, combat, progression)
2. Game loop (win/lose conditions)
3. Levels (3-5 levels with descriptions)
4. Weapons/Items (if applicable)
5. Enemies (3-5 types)
6. UI elements

Format as valid JSON.
"""

        result = await self.code_generator.generate_from_prompt(design_prompt)

        return TaskResult(
            success=result.success,
            output=result.code if result.success else None,
            metrics={
                "design_generated": result.success,
                "genre": genre
            },
            error=result.error
        )

    async def _handle_refactoring(
        self,
        prompt: str,
        context: Dict[str, Any],
        intent: Intent
    ) -> TaskResult:
        """Handle code refactoring."""
        self.logger.info("Handling refactoring")

        code = context.get("code", "")

        refactor_prompt = f"""
Refactor the following code to improve:
- Code organization
- Readability
- SOLID principles adherence
- Proper abstractions

Code:
```
{code}
```

Instructions: {prompt}

Return the refactored code with brief comments on major changes.
"""

        result = await self.code_generator.generate_from_prompt(refactor_prompt)

        return TaskResult(
            success=result.success,
            output=result.code if result.success else None,
            metrics={"refactored": result.success},
            error=result.error
        )

    async def _handle_documentation(
        self,
        prompt: str,
        context: Dict[str, Any],
        intent: Intent
    ) -> TaskResult:
        """Handle documentation generation."""
        self.logger.info("Handling documentation")

        code = context.get("code", "")

        doc_prompt = f"""
Generate comprehensive documentation for:

```
{code}
```

Include:
1. Brief overview
2. Function/class descriptions
3. Parameter documentation
4. Return value documentation
5. Usage examples
6. Notes on edge cases

Format in Doxygen style for C++ or docstrings for Python.
"""

        result = await self.code_generator.generate_from_prompt(doc_prompt)

        return TaskResult(
            success=result.success,
            output=result.code if result.success else None,
            metrics={"documented": result.success},
            error=result.error
        )

    def _build_context(
        self,
        prompt: str,
        context: Dict[str, Any]
    ) -> Dict[str, Any]:
        """Build comprehensive context for code generation."""
        return {
            "prompt": prompt,
            "project_context": context,
            "coding_guidelines": self._get_coding_guidelines(),
            "constraints": context.get("constraints", {}),
            "language": context.get("language", "cpp")
        }

    def _get_coding_guidelines(self) -> Dict[str, Any]:
        """Return coding guidelines and best practices."""
        return {
            "language": "C++17",
            "style": "Google C++ Style Guide",
            "naming": {
                "classes": "PascalCase",
                "functions": "PascalCase",
                "variables": "snake_case_",
                "constants": "kPascalCase"
            },
            "best_practices": [
                "Use smart pointers instead of raw pointers",
                "Prefer const references for large objects",
                "Use RAII for resource management",
                "Avoid global state",
                "Write self-documenting code"
            ]
        }

    def _extract_genre(self, prompt: str) -> str:
        """Extract game genre from prompt."""
        prompt_lower = prompt.lower()
        genres = ["fps", "rpg", "platformer", "puzzle", "racing", "strategy", "action"]

        for genre in genres:
            if genre in prompt_lower:
                return genre

        return "fps"  # Default

    def _detect_language(
        self,
        prompt: str,
        context: Optional[Dict[str, Any]]
    ) -> str:
        """Detect programming language from context."""
        if context and "language" in context:
            return context["language"]

        prompt_lower = prompt.lower()

        if any(kw in prompt_lower for kw in ["python", "py", ".py"]):
            return "python"
        if any(kw in prompt_lower for kw in ["shader", "glsl", "hlsl"]):
            return "glsl"

        return "cpp"  # Default for engine code

    def get_interaction_history(self) -> List[Interaction]:
        """Get interaction history for analysis."""
        return self.interactions

    def get_success_rate(self) -> float:
        """Calculate success rate of interactions."""
        if not self.interactions:
            return 0.0

        successes = sum(1 for i in self.interactions if i.result.success)
        return successes / len(self.interactions)
