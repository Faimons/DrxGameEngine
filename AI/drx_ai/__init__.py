"""
DRX AI - Autonomous Game Engine Development System

This module provides AI-powered code generation, optimization,
and game creation capabilities for the DRX Engine.
"""

__version__ = "1.0.0"
__author__ = "DRX Team"

from .core.orchestrator import ClaudeCodeOrchestrator
from .core.code_generator import CodeGenerator

__all__ = [
    "ClaudeCodeOrchestrator",
    "CodeGenerator",
]
