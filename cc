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

# Words that don't carry topic meaning
STOPWORDS = frozenset({
    "the", "a", "an", "is", "are", "was", "were", "be", "been", "being",
    "have", "has", "had", "do", "does", "did", "will", "would", "could",
    "should", "may", "might", "shall", "can", "need", "must",
    "i", "you", "we", "they", "he", "she", "it", "me", "my", "your",
    "our", "their", "its", "this", "that", "these", "those",
    "what", "which", "who", "whom", "where", "when", "how", "why",
    "not", "no", "yes", "ok", "okay", "sure", "yeah", "just", "also",
    "and", "or", "but", "if", "then", "than", "so", "too", "very",
    "of", "in", "on", "at", "to", "for", "with", "from", "by", "about",
    "into", "through", "up", "out", "down", "off", "over", "under",
    "all", "any", "some", "each", "every", "both", "few", "more",
    "other", "new", "now", "here", "there", "don", "doesn", "didn",
    "won", "wouldn", "couldn", "shouldn", "let", "lets", "get",
    "like", "going", "want", "right", "way", "thing", "things",
    "really", "actually", "still", "already", "back", "much",
})

# Verbs that describe actions but not topics
ACTION_VERBS = frozenset({
    "run", "make", "do", "try", "check", "look", "see", "go", "use",
    "put", "take", "give", "tell", "show", "find", "keep", "think",
    "know", "want", "like", "come", "turn", "work", "call", "move",
    "set", "add", "change", "start", "open", "close", "read", "write",
    "pull", "push", "test", "update", "install", "commit", "resume",
    "please", "help", "implement", "following", "plan",
    "create", "build", "fix", "design", "search", "review",
    "download", "delete", "remove", "rename", "copy", "paste",
    "save", "load", "import", "export", "send", "receive",
})

# Code/schema tokens that leak from structured content
CODE_NOISE = frozenset({
    "type", "string", "description", "required", "default", "null",
    "true", "false", "boolean", "number", "object", "array",
    "json", "yaml", "xml", "html", "css", "http", "https",
    "file", "files", "path", "dir", "name", "value", "key",
    "error", "status", "data", "input", "output", "result",
    "command", "message", "content", "text", "param", "params",
    "settings", "config", "options", "args", "env", "var",
    "completed", "running", "pending", "failed", "success",
    "task", "tasks", "hook", "hooks", "says",
    "tool", "tools", "source", "properties", "items", "item",
    "only", "bash", "edit", "glob", "grep", "agent", "agents",
    "user", "users", "mode", "auto", "prompt", "prompts",
    "claude", "CLAUDE", "session", "sessions",
    "rule", "rules", "order", "request", "response",
    "login", "working", "enable-auto-mode", "auto-mode",
    "interrupted", "continue", "recent",
    "const", "let", "var", "function", "class", "return", "async",
    "await", "import", "require", "module", "exports", "define",
    "model", "schema", "interface", "enum", "struct",
    "posttooluse", "pretooluse", "matcher", "callback", "handler",
    "runs", "after", "before", "above", "below",
    "permission", "allowed", "denied", "granted",
    "most", "that's", "i'm", "it's", "don't", "can't", "won't",
    "didn't", "doesn't", "isn't", "aren't", "wasn't", "weren't",
    "first", "last", "next", "previous", "current",
    "same", "different", "specific", "general", "entire",
    "level", "similar", "existing", "repo",
    "allow", "allowed", "allows", "remote", "phone", "call",
    "hours", "minutes", "seconds", "time", "date", "day",
    "server", "servers", "client", "port", "host",
    "shot", "cant", "wont", "dont", "didnt", "doesnt",
    "well", "good", "great", "bad", "thing", "stuff",
    "sys", "bin", "tmp", "log", "logs", "pid",
    "start", "stop", "done", "ready", "wait",
    "yeah", "yep", "nah", "nope", "hmm", "huh",
    "need", "needs", "got", "getting", "goes",
    "know", "look", "looks", "feel", "feels",
    "makes", "made", "takes", "took", "gives", "gave",
    "version", "number", "count", "total", "list",
    "going", "try", "trying", "using", "based",
    "point", "part", "side", "line", "lines",
    "issue", "problem", "bug", "fix", "fixed",
    "please", "should", "would", "could",
    "local", "global", "without", "within", "between", "across",
    "control", "process", "system", "program", "running",
    "three", "four", "five", "every", "many", "much",
    "woudl", "teh", "hte", "taht", "adn", "wiht",  # common typos
    "whats", "thats", "heres", "theres", "lets",
    "pretty", "really", "quite", "super", "extra",
    "assess", "grade", "grading", "graded", "grades",
    "remote-control", "remote", "account", "studio",
    "spending", "money", "cost", "price", "free", "paid",
    "applicable", "available", "possible", "necessary",
    "expression", "interval", "option", "options",
    "instructions", "permissioning", "permission",
    "launched", "awesome", "low-risk", "preview",
    "perfect", "exactly", "correct", "wrong",
    "specificity", "generality", "complexity",
    "pick", "recurring", "smartest", "research",
    "march", "april", "january", "february",
    "carrie", "jeremy", "schein",  # personal names
    "your-key-here", "anthropic_api_key",
    "autonomously", "approves", "actions", "approved",
    "croncreate", "crondelete", "cronlist",
    "days", "pushes", "execution",
    "against", "ones", "escalates", "higher-risk",
    "implementation", "approach", "strategy",
    "phases", "phase", "deliverable", "deliverables",
})

# Exact user messages to ignore
USER_NOISE = frozenset({
    "auto mode", "auto", "cc", "claude", "claude code", "test", "yes",
    "no", "ok", "okay", "sure", "thanks", "thank you", "-", "done",
    "continue", "go", "next", "stop", "wait", "undo", "cancel",
})

# Generic path components to skip
KNOWN_ACRONYMS = frozenset({
    "api", "mlb", "npm", "mcp", "sme", "svg", "css",
    "sql", "cli", "sdk", "cdn", "dns", "jwt", "aws",
    "gcp", "llm", "ux", "ui", "pr", "ci", "cd",
    "dtc", "nyc", "nfl", "nba", "ip", "url",
})

GENERIC_PATH_PARTS = frozenset({
    "src", "app", "lib", "dist", "build", "output", "outputs", "node_modules",
    "index", "__init__", "route", "page", "layout", "main", "utils", "helpers",
    "services", "components", "public", "static", "assets", "config", "scripts",
})

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


def extract_topic_keywords(user_messages, skip_first=True):
    """Extract meaningful topic words from user messages.

    Skips the first message (already used as title) and weights
    early substantive messages higher than late meta-conversation.
    """
    counts = Counter()
    msgs = user_messages[1:] if skip_first and len(user_messages) > 1 else user_messages
    for i, msg in enumerate(msgs):
        # Skip noise messages
        stripped = msg.strip()
        if stripped.lower() in USER_NOISE:
            continue
        if stripped.startswith("<"):
            continue
        if len(stripped) < 3:
            continue
        # Weight: messages 1-5 get 3x, 6-10 get 2x, rest get 1x
        weight = 3 if i < 5 else (2 if i < 10 else 1)
        # Clean and tokenize
        clean = re.sub(r'<[^>]+>', '', msg)  # strip XML tags
        clean = re.sub(r'https?://\S+', '', clean)  # strip URLs
        clean = re.sub(r'[/\\]\S+', '', clean)  # strip file paths
        tokens = re.findall(r"[a-zA-Z][\w'-]*", clean)
        for tok in tokens:
            low = tok.lower()
            if len(low) < 3:
                continue
            if low in STOPWORDS or low in ACTION_VERBS or low in CODE_NOISE:
                continue
            # Skip short generic tokens (keep known acronyms)
            if len(low) <= 3 and low not in KNOWN_ACRONYMS:
                continue
            # Skip likely typos: words with unusual letter patterns
            # (no vowels, or mostly consonant clusters)
            vowels = sum(1 for c in low if c in "aeiou")
            if len(low) >= 4 and vowels == 0:
                continue  # no vowels = likely garbage
            if low.endswith("ign") or low.endswith("oin") or low.endswith("odl"):
                continue  # common typo patterns: "workign", "sessoin", "woudl"
            # Skip words with 3+ consecutive consonants that aren't common
            if re.search(r'[^aeiou]{4,}', low) and low not in KNOWN_ACRONYMS:
                continue  # "braodly", "btwn" etc.
            # Keep original case for known acronyms only
            if tok.isupper() and len(tok) <= 5 and len(tok) >= 2 and low in KNOWN_ACRONYMS:
                counts[tok] += weight
            else:
                counts[low] += weight
    return counts


def extract_path_keywords(files_edited, files_created):
    """Extract meaningful topic words from file paths."""
    counts = Counter()
    home = str(Path.home())
    for fp in list(files_edited) + list(files_created):
        if fp.startswith(home + "/"):
            fp = fp[len(home) + 1:]
        # Skip .claude internal files
        if fp.startswith(".claude"):
            continue
        parts = fp.split("/")
        for part in parts:
            name = os.path.splitext(part)[0]  # strip extension
            # Split hyphenated/underscored names
            subparts = re.split(r'[-_.]', name)
            for sp in subparts:
                low = sp.lower()
                if len(low) < 3 or low in GENERIC_PATH_PARTS:
                    continue
                counts[low] += 1
    return counts


def _normalize_keyword(word):
    """Normalize plurals and common suffixes to reduce duplication."""
    low = word.lower()
    # Simple plural stripping (skills->skill, events->event, agents->agent)
    if low.endswith("s") and len(low) > 4 and not low.endswith("ss"):
        return low[:-1]
    return low


def _clean_user_snippet(msg):
    """Clean a user message into a short readable snippet."""
    text = msg.replace("\n", " ").strip()
    text = re.sub(r'<[^>]+>', '', text).strip()  # strip XML tags
    text = re.sub(r'https?://\S+', '', text).strip()  # strip URLs
    text = re.sub(r'[/\\]\S+/\S+', '', text).strip()  # strip file paths
    text = re.sub(r'^Caveat:.*?references them\.?\s*', '', text).strip()
    text = re.sub(r'^DO NOT respond.*?references them\.?\s*', '', text).strip()
    text = re.sub(r'^Implement the following plan:\s*', '', text).strip()
    text = re.sub(r'#\s*Plan:\s*', '', text).strip()
    text = re.sub(r'^Base directory for this skill:.*', '', text).strip()
    text = re.sub(r'^This session is being continued.*', '', text).strip()
    text = re.sub(r'\[Request interrupted.*?\]', '', text).strip()
    # Strip task notification noise
    text = re.sub(r'<task-notification>.*?</task-notification>', '', text).strip()
    text = re.sub(r'[a-f0-9]{16,}', '', text).strip()  # hex IDs
    text = re.sub(r'toolu_\S+', '', text).strip()  # tool use IDs
    # Take first sentence
    text = re.split(r'[.!?\n]', text)[0].strip()
    # Normalize ALL CAPS to title case (less jarring)
    if text.isupper() and len(text) > 10:
        text = text.title()
    # Lowercase check for noise
    low = text.lower().strip()
    if low in USER_NOISE or len(low) < 4:
        return None
    # Skip slash commands, login noise, errors, and agent output
    if low.startswith("/") or low.startswith("cd ") or low.startswith("export "):
        return None
    if low.startswith("login") or low.startswith("error:") or low.startswith("completed agent"):
        return None
    if "interrupted" in low:
        return None
    # Skip skill/plugin definitions and escaped paths
    if low.startswith("# /") or low.startswith("#/"):
        return None
    if "\\" in text:  # escaped shell paths
        return None
    return text


def _first_bash_description(bash_descriptions):
    """Pick the first substantive bash description — it often captures purpose."""
    SKIP = {"list files", "show working", "show tree", "check current",
            "show git", "list contents", "check if", "run cc", "check for",
            "list files in current", "show status", "check version",
            "list directory", "read file", "show output"}
    for desc in bash_descriptions:
        dl = desc.lower()
        if any(dl.startswith(s) or s in dl for s in SKIP):
            continue
        if len(desc) < 15:
            continue
        return desc[:75]
    return None


# Intent verbs that signal the PURPOSE of a session
_INTENT_RE = re.compile(
    r'\b(improve|refine|optimize|fix|build|create|configure|setup|set up|'
    r'migrate|deploy|design|implement|install|upgrade|scaffold|'
    r'review|analyze|debug|rewrite|restructure|convert|extract|generate|'
    r'simulate|stream|download|enable|turn on|activate)\b\s+(.+)',
    re.IGNORECASE
)

# "I want to X" / "I need to X" patterns — strong purpose signals
_WANT_RE = re.compile(
    r'i\s+(?:want|need|would like|wanna|\'d like)\s+(?:you\s+)?(?:to\s+)?(.{15,})',
    re.IGNORECASE
)


def _extract_intent(snippet):
    """Try to extract a verb+object intent phrase from a user snippet.

    Only returns a match if the object is substantial enough to be a purpose
    statement (15+ chars). Short matches like 'run it' are skipped.
    """
    # Try "I want to X" first — strongest purpose signal
    m = _WANT_RE.search(snippet)
    if m:
        obj = m.group(1).strip()
        obj = re.split(r'[.!?\n]', obj)[0].strip()
        if len(obj) >= 15:
            return obj[:75]

    # Try intent verb + object
    m = _INTENT_RE.search(snippet)
    if m:
        verb = m.group(1).lower()
        obj = m.group(2).strip()
        # Take up to the first comma, dash, or sentence break
        obj = re.split(r'[,\-—;]', obj)[0].strip()
        # Must be a substantial phrase, not "run it" or "add this"
        if len(obj) >= 15:
            return f"{verb} {obj}"[:75]
    return None


def build_summary(user_messages, files_edited, files_created, bash_descriptions,
                  plan_title=None, title="", project=""):
    """Build a purpose-driven summary of what the session was about.

    Strategy: find the user's INTENT — what they wanted to accomplish.
    Priority: plan title > intent phrase from user messages > first bash description > longest snippet.
    """
    # 1. Plan title — best signal when available
    if plan_title:
        return plan_title[:80]

    # 2. Also try the first message (title source) — for short sessions it IS the purpose
    first_intent = None
    if user_messages:
        first_snippet = _clean_user_snippet(user_messages[0])
        if first_snippet:
            first_intent = _extract_intent(first_snippet)

    # 3. Collect cleaned user message snippets from messages 2+
    snippets = []
    title_low = (title + " " + project).lower()
    for msg in user_messages[1:20]:
        snippet = _clean_user_snippet(msg)
        if not snippet:
            continue
        if snippet.lower().strip() == title_low.strip():
            continue
        if len(snippet) < 12:
            continue
        snippets.append(snippet)

    # 4. Collect ALL intent phrases from later messages, pick the longest
    intents = []
    for snippet in snippets:
        intent = _extract_intent(snippet)
        if intent:
            intents.append(intent)

    if intents:
        best_intent = max(intents, key=len)
        return best_intent

    # 5. If no intent from later messages, try the first message's intent
    if first_intent:
        return first_intent

    # 6. Try the first substantive bash description
    bash_summary = _first_bash_description(bash_descriptions)

    # 7. Fallback: longest snippet from first 5 messages (where purpose lives)
    early_candidates = [s for s in snippets[:5] if len(s) >= 20]
    if early_candidates:
        best_snippet = max(early_candidates, key=len)[:75]
        # If bash description exists and is more specific, prefer it
        if bash_summary and len(bash_summary) > len(best_snippet):
            return bash_summary
        return best_snippet

    if bash_summary:
        return bash_summary

    # 8. Last resort: any snippet that's actually informative (20+ chars, not a question)
    if snippets:
        informative = [s for s in snippets if len(s) >= 20 and not s.strip().endswith("?")
                       and not s.lower().startswith(("it sys", "the remote", "how do"))]
        if informative:
            return max(informative, key=len)[:75]

    return ""


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
        plan_title = None

        try:
            with open(path) as f:
                for line in f:
                    try:
                        obj = json.loads(line)
                    except json.JSONDecodeError:
                        continue

                    if slug is None and "slug" in obj:
                        slug = obj["slug"]

                    # Extract plan title if present
                    if plan_title is None and "planContent" in obj:
                        pc = obj["planContent"]
                        if pc:
                            m = re.search(r'#\s*Plan:\s*(.+?)(?:\s*[\n—\-]|$)', pc)
                            if m:
                                plan_title = m.group(1).strip()

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
            session_title = auto_title(first_message)
            session_project = best_project(cwd_counts) or ""
            summary = build_summary(
                user_messages, files_edited, files_created, bash_descriptions,
                plan_title, title=session_title, project=session_project,
            )
            sessions.append({
                "session_id": session_id,
                "timestamp": timestamp,
                "last_active": last_timestamp or timestamp,
                "first_message": first_message,
                "title": session_title,
                "summary": summary,
                "slug": slug,
                "project": session_project or None,
                "user_turns": user_turns,
                "assistant_turns": assistant_turns,
                "duration": duration,
                "tools_used": sorted(tools_used - {""}),
                "files_edited": sorted(files_edited),
                "files_created": sorted(files_created),
                "search_text": " ".join(user_messages[:20]).lower(),
            })

    # Non-empty summaries first (by date desc), then empty summaries at bottom (by date desc)
    sessions.sort(key=lambda s: (bool(s.get("summary")), s["last_active"]), reverse=True)
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


GENERIC_TITLES = frozenset({
    "update claude", "update claude code", "update", "auto mode", "auto",
    "cc", "test", "is this working", "-", "claude", "remote-control",
    "export anthropic_api_key", "login",
})


def get_display_name(session, tags):
    """Get the best name for a session: user tag > summary (if title is generic) > auto-title."""
    tag = tags.get(session["session_id"])
    if tag:
        return tag
    title = session.get("title") or ""
    summary = session.get("summary") or ""
    # If the title is generic/meta, use the summary as the display name
    if title.lower().strip() in GENERIC_TITLES or len(title.strip()) <= 2:
        if summary and len(summary) > 10:
            return summary[:60]
    # If the title starts with generic prefixes, prefer summary
    title_low = title.lower().strip()
    if title_low.startswith(("update clau", "update code")):
        if summary and len(summary) > 10:
            return summary[:60]
    return title or truncate(session["first_message"], 50)


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

    # Line 2: stats — started, last active, turns, duration, tools
    start_date = format_date(session["timestamp"])
    start_time = format_time(session["timestamp"])
    stats = []
    stats.append(f"started {start_date} {start_time}")
    stats.append(f"last {last_date} {last_time}")
    stats.append(f"{turns} turns")
    if duration:
        stats.append(duration)
    tool_str = format_tools(tools)
    if tool_str:
        stats.append(tool_str)
    line2 = f"        {dim(' · '.join(stats))}"

    lines = [line1, line2]

    # Line 3: summary — purpose of the session (skip if already used as display name)
    if summary and summary[:60] != name[:60]:
        summary_display = summary[:90]
        lines.append(f"        {yellow(summary_display)}")

    # Line 4: file activity context for substantial sessions
    n_edited = len(session.get("files_edited", []))
    n_created = len(session.get("files_created", []))
    n_files = n_edited + n_created
    if n_files > 0:
        file_note = f"{n_files} files touched"
        if n_edited and n_created:
            file_note = f"{n_edited} edited, {n_created} created"
        lines.append(f"        {dim(file_note)}")

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
        summary = (session.get("summary") or "").lower()
        search_text = session.get("search_text", "")
        if keyword_lower in msg or keyword_lower in tag or keyword_lower in title or keyword_lower in project or keyword_lower in summary or keyword_lower in search_text:
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
