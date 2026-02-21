# cc — Claude Conversations

A tiny CLI to find, search, tag, and resume your past [Claude Code](https://docs.anthropic.com/en/docs/claude-code) sessions.

```
$ cc

  11 sessions (2026-02-13 to 2026-02-20)

    1  feb 20  10:31pm   APEX app
    2  feb 18  8:33pm    Find my schoology app
    3  feb 17  4:26pm    Find my previous claude projects
    4  feb 17  10:06am   crest-design
    5  feb 16  5:44pm    Rebuild v4
    6  feb 15  8:31am    Create a brand design for "rim and racquet"
    7  feb 13  9:34am    pe-brief

  Resume #: 4
  Resuming crest-design...
```

## Install

```bash
git clone https://github.com/YOUR_USERNAME/cc-claude.git
cd cc-claude
bash install.sh
```

Or just copy the single file:

```bash
curl -o ~/.local/bin/cc https://raw.githubusercontent.com/YOUR_USERNAME/cc-claude/main/cc
chmod +x ~/.local/bin/cc
```

### Requirements

- Python 3.8+
- Claude Code CLI (`claude`) installed
- macOS or Linux

## Usage

| Command | What it does |
|---------|-------------|
| `cc` | List all sessions, pick one to resume |
| `cc <keyword>` | Search sessions by keyword |
| `cc tag <#> <name>` | Tag a session with a friendly name |
| `cc resume <#>` | Resume a specific session |
| `cc help` | Show usage |

### Identify sessions by

- **Number** — `cc resume 3`
- **UUID prefix** — `cc resume 38a50c8e`
- **Tag name** — `cc resume crest-design`

### Examples

```bash
cc                        # list all, pick one
cc brand                  # search for "brand"
cc tag 1 my-project       # tag session #1
cc resume my-project      # resume by tag
```

## How it works

- Scans `~/.claude/projects/**/*.jsonl` for session files
- Reads just the first few lines of each file to extract the date and first message
- Auto-generates short titles from message content (plan names, verbs, project names)
- Tags are stored in `~/.claude/session-tags.json`
- Resumes sessions via `claude --resume <session-id>`

Single Python file, zero dependencies, stdlib only.

## License

MIT
