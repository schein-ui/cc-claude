#!/usr/bin/env python3
"""cc - Claude Conversations: find and resume past Claude Code sessions."""

import sys
import os
import json
import re
from pathlib import Path
from datetime import datetime
from collections import Counter

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
def cyan(s):   return f"\033[36m{s}\033[0m" if USE_COLOR else s
def yellow(s): return f"\033[33m{s}\033[0m" if USE_COLOR else s


# === Core Functions ===

NOISE_PATTERNS = [
    re.compile(r'<local-command-caveat>'),
    re.compile(r'^Caveat: The messages below were generated'),
    re.compile(r'^Unknown skill:'),
    re.compile(r'^/\w+\s*$'),  # bare slash commands like "/voice"
    re.compile(r'^\s*$'),
]


def is_noise(text):
    """Check if a message is system noise rather than real user input."""
    return any(p.search(text) for p in NOISE_PATTERNS)


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
    # Strip XML-like tags (command messages, caveats, etc.)
    text = re.sub(r'<[^>]+>', '', text).strip()
    # Strip common noise prefixes (caveat messages from local commands)
    text = re.sub(r'^Caveat:.*?(?:unless the user explicitly references them\.?\s*)', '', text, count=1).strip()
    if not text or text.startswith("DO NOT"):
        text = re.sub(r'^DO NOT respond.*?(?:references them\.?\s*)', '', text, count=1).strip()
    # A bare file path as the message → use the filename
    m_path = re.match(r'^(/\S+/)([^/\s]+)\s*$', text)
    if m_path:
        return f"Open {m_path.group(2)}"

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


def project_from_cwd(cwd):
    """Extract a short project name from the working directory."""
    if not cwd:
        return None
    home = str(Path.home())
    if cwd == home:
        return None  # home dir, no specific project
    if cwd.startswith(home + "/"):
        rel = cwd[len(home) + 1:]
        # First path component is the project
        return rel.split("/")[0]
    return os.path.basename(cwd)


def best_project(cwd_counts):
    """Pick the best project from a counter of cwds seen in a session."""
    home = str(Path.home())
    # Prefer non-home directories
    for cwd, _count in cwd_counts.most_common():
        if cwd != home:
            return project_from_cwd(cwd)
    return None


def size_indicator(user_turns):
    """Return a size dot based on session depth."""
    if user_turns <= 3:
        return dim("·")    # tiny
    if user_turns <= 15:
        return dim("●")    # small
    if user_turns <= 50:
        return "●"         # medium
    return bold("●")       # large


def format_duration(start_iso, end_iso):
    """Return a human-readable duration string between two ISO timestamps."""
    try:
        start = datetime.fromisoformat(start_iso.replace("Z", "+00:00"))
        end = datetime.fromisoformat(end_iso.replace("Z", "+00:00"))
        delta = end - start
        total_secs = int(delta.total_seconds())
        if total_secs < 60:
            return "<1m"
        if total_secs < 3600:
            return f"{total_secs // 60}m"
        hours = total_secs // 3600
        mins = (total_secs % 3600) // 60
        if mins:
            return f"{hours}h {mins}m"
        return f"{hours}h"
    except (ValueError, OSError):
        return ""


def short_path(fp):
    """Shorten a file path for display."""
    home = str(Path.home())
    if fp.startswith(home + "/"):
        fp = fp[len(home) + 1:]
    # Further shorten: just filename if path is long
    parts = fp.split("/")
    if len(parts) > 3:
        return f".../{'/'.join(parts[-2:])}"
    return fp


def build_summary(user_messages, files_edited, files_created, bash_descriptions):
    """Build a concise summary of what happened in the session."""
    parts = []

    # 1. Best signal: bash command descriptions (Claude wrote these as clean English)
    if bash_descriptions:
        unique_descs = []
        seen = set()
        for d in bash_descriptions:
            # Normalize and dedup
            dl = d.lower().strip()
            # Skip generic ones
            if dl in seen or dl in ("list files in current directory", "show working tree status"):
                continue
            seen.add(dl)
            unique_descs.append(d)
        # Pick the most descriptive ones (longer = more specific)
        unique_descs.sort(key=len, reverse=True)
        top = unique_descs[:3]
        if top:
            parts.append("; ".join(top))

    # 2. Files touched — compact summary
    all_files = set()
    for fp in files_edited:
        all_files.add(short_path(fp))
    for fp in files_created:
        all_files.add(short_path(fp))

    if all_files:
        if len(all_files) <= 3:
            parts.append(", ".join(sorted(all_files)))
        else:
            exts = Counter()
            dirs = Counter()
            for f in all_files:
                ext = os.path.splitext(f)[1]
                if ext:
                    exts[ext] += 1
                d = os.path.dirname(f)
                if d:
                    # Use just the last dir component
                    dirs[os.path.basename(d)] += 1
            file_summary = f"{len(all_files)} files"
            # Show top directories
            top_dirs = [d for d, _ in dirs.most_common(2)]
            if top_dirs:
                file_summary += f" in {', '.join(top_dirs)}"
            else:
                ext_str = ", ".join(f"{e}" for e, c in exts.most_common(3))
                if ext_str:
                    file_summary += f" ({ext_str})"
            parts.append(file_summary)

    return " · ".join(parts) if parts else ""


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
        user_messages = []
        cwd_counts = Counter()
        user_turns = 0
        last_timestamp = None
        tools_used = set()
        assistant_turns = 0
        files_edited = set()
        files_created = set()
        bash_descriptions = []
        key_topics = set()

        try:
            with open(path) as f:
                for line in f:
                    try:
                        obj = json.loads(line)
                    except json.JSONDecodeError:
                        continue

                    if slug is None and "slug" in obj:
                        slug = obj["slug"]

                    ts = obj.get("timestamp")
                    if ts:
                        last_timestamp = ts

                    cwd = obj.get("cwd")
                    if cwd:
                        cwd_counts[cwd] += 1

                    if obj.get("type") == "user":
                        user_turns += 1
                        content = extract_content(obj.get("message", {}).get("content"))
                        if content and not is_noise(content):
                            if first_message is None:
                                first_message = content
                                timestamp = ts
                            if len(user_messages) < 20:
                                user_messages.append(content)

                    if obj.get("type") == "assistant":
                        assistant_turns += 1
                        msg = obj.get("message", {})
                        content = msg.get("content", [])
                        if isinstance(content, list):
                            for item in content:
                                if isinstance(item, dict) and item.get("type") == "tool_use":
                                    tool_name = item.get("name", "")
                                    tools_used.add(tool_name)
                                    inp = item.get("input", {})
                                    # Track files edited/created
                                    if tool_name == "Edit":
                                        fp = inp.get("file_path", "")
                                        if fp:
                                            files_edited.add(fp)
                                    elif tool_name == "Write":
                                        fp = inp.get("file_path", "")
                                        if fp:
                                            files_created.add(fp)
                                    elif tool_name == "Bash":
                                        desc = inp.get("description", "")
                                        if desc and len(bash_descriptions) < 30:
                                            bash_descriptions.append(desc)
        except (OSError, IOError):
            continue

        if first_message and timestamp:
            duration = format_duration(timestamp, last_timestamp or timestamp)
            summary = build_summary(user_messages, files_edited, files_created, bash_descriptions)
            sessions.append({
                "session_id": session_id,
                "timestamp": timestamp,
                "last_active": last_timestamp or timestamp,
                "first_message": first_message,
                "title": auto_title(first_message),
                "summary": summary,
                "slug": slug,
                "project": best_project(cwd_counts),
                "user_turns": user_turns,
                "assistant_turns": assistant_turns,
                "duration": duration,
                "tools_used": sorted(tools_used - {""}),
                "files_edited": sorted(files_edited),
                "files_created": sorted(files_created),
            })

    sessions.sort(key=lambda s: s["last_active"], reverse=True)
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


TOOL_LABELS = {
    "Read": "read",
    "Write": "write",
    "Edit": "edit",
    "Bash": "bash",
    "Glob": "glob",
    "Grep": "grep",
    "Agent": "agent",
    "WebSearch": "web",
    "WebFetch": "web",
}


def format_tools(tools):
    """Return a compact summary of tools used."""
    labels = set()
    for t in tools:
        label = TOOL_LABELS.get(t)
        if label:
            labels.add(label)
        elif t.startswith("mcp__"):
            labels.add("mcp")
    if not labels:
        return ""
    return ", ".join(sorted(labels))


def format_session_line(num, session, tags):
    last_date = format_date(session["last_active"])
    last_time = format_time(session["last_active"])
    name = get_display_name(session, tags)
    is_tagged = session["session_id"] in tags
    project = session.get("project")
    turns = session.get("user_turns", 0)
    duration = session.get("duration", "")
    tools = session.get("tools_used", [])

    num_str = bold(str(num).rjust(3))
    date_part = dim(f"{last_date}  {last_time}".ljust(16))
    size = size_indicator(turns)

    if is_tagged:
        name_part = green(name)
    else:
        name_part = white(name)

    proj_part = f"  {cyan(project)}" if project else ""

    summary = session.get("summary", "")

    # Line 1: number, date, title, project
    line1 = f"  {num_str}  {date_part}  {size} {name_part}{proj_part}"

    # Line 2: stats — turns, duration, tools
    stats = []
    stats.append(f"{turns} turns")
    if duration:
        stats.append(duration)
    tool_str = format_tools(tools)
    if tool_str:
        stats.append(tool_str)
    line2 = f"        {dim(' · '.join(stats))}"

    lines = [line1, line2]

    # Line 3: summary of what happened
    if summary:
        # Truncate to terminal width
        summary_display = summary[:90]
        if len(summary) > 90:
            summary_display += "..."
        lines.append(f"        {yellow(summary_display)}")

    lines.append("")  # blank line between sessions

    return "\n".join(lines)


# === Commands ===

def cmd_list(sessions, tags, interactive=True, project_filter=None):
    if project_filter:
        pf = project_filter.lower()
        sessions = [s for s in sessions if (s.get("project") or "").lower().startswith(pf)]

    if not sessions:
        if project_filter:
            print(f"No sessions in project '{project_filter}'.")
        else:
            print("No sessions found.")
        return

    oldest = sessions[-1]["last_active"][:10]
    newest = sessions[0]["last_active"][:10]
    header = f"{len(sessions)} sessions"
    if project_filter:
        header += f" in {project_filter}"
    header += f" ({oldest} to {newest})"
    print(bold(f"\n  {header}\n"))

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
        project = (session.get("project") or "").lower()
        if keyword_lower in msg or keyword_lower in tag or keyword_lower in title or keyword_lower in project:
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
  cc                          List all sessions (sorted by last active)
  cc <keyword>                Search sessions by keyword
  cc @<project>               Filter by project directory
  cc tag <#> <name>           Tag a session with a friendly name
  cc resume [#]               Resume a session (interactive if no arg)
  cc help                     Show this help

Sessions can be identified by:
  - List number (e.g., 3)
  - UUID or UUID prefix (e.g., 38a50c8e)
  - Tag name (e.g., crest-design)

Each line shows:  #  last-active  size  title  project
  Size: · tiny (≤3 turns)  ● small  ● medium  ● large (50+)

Examples:
  cc brand                    Find sessions mentioning "brand"
  cc @brand-builder           Show only brand-builder sessions
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
    elif args[0].startswith("@"):
        # @project filter: cc @brand-builder
        project_filter = args[0][1:]
        cmd_list(sessions, tags, project_filter=project_filter)
    else:
        # Unknown arg = search
        cmd_search(sessions, tags, " ".join(args))


if __name__ == "__main__":
    main()
