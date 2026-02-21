#!/usr/bin/env python3
"""cc - Claude Conversations: find and resume past Claude Code sessions."""

import sys
import os
import json
import re
from pathlib import Path
from datetime import datetime

# === Constants ===
SESSIONS_DIR = Path.home() / ".claude" / "projects"
TAGS_FILE = Path.home() / ".claude" / "session-tags.json"
MAX_MSG_LEN = 80

# === Color Helpers ===
USE_COLOR = hasattr(sys.stdout, "isatty") and sys.stdout.isatty()

def dim(s):    return f"\033[2m{s}\033[0m" if USE_COLOR else s
def green(s):  return f"\033[32m{s}\033[0m" if USE_COLOR else s
def bold(s):   return f"\033[1m{s}\033[0m" if USE_COLOR else s
def white(s):  return f"\033[97m{s}\033[0m" if USE_COLOR else s


# === Core Functions ===

def extract_content(content):
    """Extract displayable text from message.content (str or list)."""
    if isinstance(content, str):
        return content.strip() if content.strip() else None
    if isinstance(content, list):
        texts = []
        for item in content:
            if isinstance(item, dict) and item.get("type") == "text":
                t = item.get("text", "").strip()
                if t and t != "[Request interrupted by user for tool use]":
                    texts.append(t)
        return " ".join(texts) if texts else None
    return None


def auto_title(msg):
    """Generate a short descriptive title from the first user message."""
    text = msg.replace("\n", " ").strip()

    # "Implement the following plan: # Plan: Actual Title Here — ..."
    m = re.search(r'#\s*Plan:\s*(.+?)(?:\s*[—\-\n]|$)', text)
    if m:
        return m.group(1).strip()[:60]

    # "I want to create/build/make ..."
    m = re.match(r'(?:I want to |I need to |help me |please |can you )?(create|build|make|implement|add|fix|write|design|find|run|search|update)\s+(.+)', text, re.IGNORECASE)
    if m:
        verb = m.group(1).capitalize()
        rest = m.group(2)
        # Truncate at first sentence boundary
        rest = re.split(r'[.!?\n]', rest)[0].strip()
        title = f"{verb} {rest}"
        return title[:60]

    # "python3 /path/to/script.py" → "Run script.py"
    m = re.match(r'(?:python3?|node|bash|sh|ruby)\s+\S*/([^\s/]+)', text)
    if m:
        return f"Run {m.group(1)}"

    # Numbered list/spec: "1. What This App Is APEX is ..." → find the app/project name
    m = re.search(r'(?:What This (?:App|Project) Is\s+)(\w[\w\s]{1,20}?)(?:\s+is\s)', text, re.IGNORECASE)
    if m:
        return f"{m.group(1).strip()} app"

    # Generic: take the first meaningful chunk, clean it up
    # Strip common prefixes
    text = re.sub(r'^(Implement the following plan:\s*)', '', text)
    # Directory trees / code dumps → use the project name if present
    if re.match(r'^[\w-]+/\s*[├└│|]', text) or text.startswith(('├', '└')):
        m = re.match(r'^([\w-]+)/', text)
        if m:
            return f"{m.group(1)} project setup"
        return "Project structure review"

    # Take first sentence or first 60 chars
    first_sentence = re.split(r'[.!?\n]', text)[0].strip()
    if len(first_sentence) > 5:
        return first_sentence[:60]

    return text[:60]


def scan_sessions():
    """Scan all .jsonl files and extract session metadata."""
    sessions = []
    for path in SESSIONS_DIR.glob("**/*.jsonl"):
        # Skip subagent files and symlinks (avoid duplicates)
        if "subagents" in path.parts:
            continue
        if path.is_symlink():
            continue

        session_id = path.stem
        slug = None
        timestamp = None
        first_message = None

        try:
            with open(path) as f:
                for line in f:
                    try:
                        obj = json.loads(line)
                    except json.JSONDecodeError:
                        continue

                    if slug is None and "slug" in obj:
                        slug = obj["slug"]

                    if obj.get("type") == "user" and first_message is None:
                        content = extract_content(obj.get("message", {}).get("content"))
                        if content:
                            first_message = content
                            timestamp = obj.get("timestamp")

                    if first_message is not None and slug is not None:
                        break
        except (OSError, IOError):
            continue

        if first_message and timestamp:
            sessions.append({
                "session_id": session_id,
                "timestamp": timestamp,
                "first_message": first_message,
                "title": auto_title(first_message),
                "slug": slug,
            })

    sessions.sort(key=lambda s: s["timestamp"], reverse=True)
    return sessions


def load_tags():
    if TAGS_FILE.exists():
        try:
            return json.loads(TAGS_FILE.read_text())
        except (json.JSONDecodeError, OSError):
            return {}
    return {}


def save_tags(tags):
    TAGS_FILE.write_text(json.dumps(tags, indent=2) + "\n")


def resolve_session(sessions, identifier):
    """Resolve a session by list number, UUID/prefix, or tag name."""
    # Try as number
    try:
        num = int(identifier)
        if 1 <= num <= len(sessions):
            return sessions[num - 1]
    except ValueError:
        pass

    # Try as UUID or prefix
    matches = [s for s in sessions if s["session_id"].startswith(identifier)]
    if len(matches) == 1:
        return matches[0]
    if len(matches) > 1:
        print(f"Ambiguous ID prefix '{identifier}' matches {len(matches)} sessions.", file=sys.stderr)
        return None

    # Try as tag name
    tags = load_tags()
    for sid, tag in tags.items():
        if tag == identifier:
            return next((s for s in sessions if s["session_id"] == sid), None)

    return None


# === Display ===

def format_date(iso_ts):
    try:
        dt = datetime.fromisoformat(iso_ts.replace("Z", "+00:00"))
        local_dt = dt.astimezone()
        return local_dt.strftime("%b %d").lower()
    except (ValueError, OSError):
        return iso_ts[:10]


def format_time(iso_ts):
    try:
        dt = datetime.fromisoformat(iso_ts.replace("Z", "+00:00"))
        local_dt = dt.astimezone()
        return local_dt.strftime("%-I:%M%p").lower()
    except (ValueError, OSError):
        return ""


def truncate(s, maxlen=MAX_MSG_LEN):
    s = s.replace("\n", " ").strip()
    return s[:maxlen] + "..." if len(s) > maxlen else s


def get_display_name(session, tags):
    """Get the best name for a session: user tag > auto-title."""
    tag = tags.get(session["session_id"])
    if tag:
        return tag
    return session.get("title") or truncate(session["first_message"], 50)


def format_session_line(num, session, tags):
    date_str = format_date(session["timestamp"])
    time_str = format_time(session["timestamp"])
    name = get_display_name(session, tags)
    is_tagged = session["session_id"] in tags

    num_str = bold(str(num).rjust(3))
    date_part = dim(f"{date_str}  {time_str}".ljust(16))
    if is_tagged:
        name_part = green(name)
    else:
        name_part = white(name)

    return f"  {num_str}  {date_part}  {name_part}"


# === Commands ===

def cmd_list(sessions, tags, interactive=True):
    if not sessions:
        print("No sessions found.")
        return

    oldest = sessions[-1]["timestamp"][:10]
    newest = sessions[0]["timestamp"][:10]
    print(bold(f"\n  {len(sessions)} sessions ({oldest} to {newest})\n"))

    for i, session in enumerate(sessions, 1):
        print(format_session_line(i, session, tags))
    print()

    if interactive:
        try:
            choice = input(bold("  Resume #: ")).strip()
        except (KeyboardInterrupt, EOFError):
            print()
            return
        if not choice:
            return
        session = resolve_session(sessions, choice)
        if not session:
            print(f"Session not found: {choice}", file=sys.stderr)
            return
        _do_resume(session, tags)


def cmd_search(sessions, tags, keyword):
    keyword_lower = keyword.lower()
    matches = []
    for i, session in enumerate(sessions, 1):
        msg = session["first_message"].lower()
        tag = tags.get(session["session_id"], "").lower()
        title = (session.get("title") or "").lower()
        if keyword_lower in msg or keyword_lower in tag or keyword_lower in title:
            matches.append((i, session))

    if not matches:
        print(f"No sessions matching '{keyword}'.")
        return

    print(bold(f"\n  {len(matches)} sessions matching '{keyword}'\n"))
    for global_num, session in matches:
        print(format_session_line(global_num, session, tags))
    print()

    try:
        choice = input(bold("  Resume #: ")).strip()
    except (KeyboardInterrupt, EOFError):
        print()
        return
    if not choice:
        return
    session = resolve_session(sessions, choice)
    if not session:
        print(f"Session not found: {choice}", file=sys.stderr)
        return
    _do_resume(session, tags)


def cmd_tag(sessions, tags, identifier, tag_name):
    session = resolve_session(sessions, identifier)
    if not session:
        print(f"Session not found: {identifier}", file=sys.stderr)
        sys.exit(1)

    tags[session["session_id"]] = tag_name
    save_tags(tags)
    msg = truncate(session["first_message"], 50)
    print(f"Tagged {dim(session['session_id'][:8] + '...')} as {green(tag_name)}")
    print(f"  {dim(msg)}")


def _do_resume(session, tags):
    """Launch claude --resume for the given session."""
    session_id = session["session_id"]
    name = get_display_name(session, tags)
    print(f"\n  Resuming {green(name)}...\n")

    env = os.environ.copy()
    env.pop("CLAUDECODE", None)
    os.execvpe("claude", ["claude", "--resume", session_id], env)


def cmd_resume(sessions, tags, identifier=None):
    if identifier is None:
        cmd_list(sessions, tags)
        return

    session = resolve_session(sessions, identifier)
    if not session:
        print(f"Session not found: {identifier}", file=sys.stderr)
        sys.exit(1)

    _do_resume(session, tags)


def print_help():
    print("""
cc - Claude Conversations

Usage:
  cc                          List all sessions (most recent first)
  cc <keyword>                Search sessions by keyword
  cc tag <#> <name>           Tag a session with a friendly name
  cc resume [#]               Resume a session (interactive if no arg)
  cc help                     Show this help

Sessions can be identified by:
  - List number (e.g., 3)
  - UUID or UUID prefix (e.g., 38a50c8e)
  - Tag name (e.g., crest-design)

Examples:
  cc brand                    Find sessions mentioning "brand"
  cc tag 1 crest-design       Tag session #1
  cc resume crest-design      Resume by tag name
  cc resume                   Pick from list
""")


# === Main ===

def main():
    args = sys.argv[1:]
    sessions = scan_sessions()
    tags = load_tags()

    if not args:
        cmd_list(sessions, tags)
    elif args[0] in ("help", "--help", "-h"):
        print_help()
    elif args[0] == "search" and len(args) >= 2:
        cmd_search(sessions, tags, " ".join(args[1:]))
    elif args[0] == "tag" and len(args) >= 3:
        cmd_tag(sessions, tags, args[1], " ".join(args[2:]))
    elif args[0] == "resume":
        identifier = args[1] if len(args) >= 2 else None
        cmd_resume(sessions, tags, identifier)
    else:
        # Unknown arg = search
        cmd_search(sessions, tags, " ".join(args))


if __name__ == "__main__":
    main()
