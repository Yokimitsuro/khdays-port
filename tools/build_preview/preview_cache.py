"""Dependency-free cache helpers for the asset preview builder."""

from pathlib import Path


def output_is_current(output: Path, *inputs: Path) -> bool:
    """Return whether a non-empty cached output is at least as new as its inputs."""
    if not output.exists() or output.stat().st_size == 0:
        return False
    output_time = output.stat().st_mtime_ns
    return all(output_time >= source.stat().st_mtime_ns for source in inputs)
