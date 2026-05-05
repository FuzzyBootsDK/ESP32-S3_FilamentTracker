# Claude Instructions for This Project

## Session Start

If they do not exist in the current directory, create them.
At the beginning of every session, read these memory files to restore context:

- [memory/preferences.md](memory/preferences.md) — how they prefer to work
- [memory/decisions.md](memory/decisions.md) — key decisions made in this project
- [memory/suggestions.md](memory/suggestions.md) — your suggestions on this Project
- [memory/thoughts.md](memory/thoughts.md) — thougts while considering what to do and how to do it

## Session End

Before ending any session where meaningful work occurred, update the relevant memory files:

- Add new decisions to `memory/decisions.md` with date and rationale.
- Update `memory/preferences.md` if new workflow or style preferences were expressed.
- Update `memory/suggestions.md` any suggestionsmade in the process of creating this project, so its easy to implement at a later date.
- Update `memory/thoughts.md` with any thoughts or thinking that happens while considering how to solve a request.

## Guidelines

- Read the project context, and ask any clarifying questions if needed before continuing, make valid suggestions on how to improve the project and wait for confirmation if they should be implemented, and then create the necessary files or code.
- Stick to this folder path: *C:\Users\lasr\OneDrive\Projects\ESP32_Filamament/*
- Keep memory files concise — capture facts and reasoning, not transcripts.
- Prefer updating existing entries over adding duplicate ones.
- If the user says "remember this", write it to the appropriate memory file immediately.
- If the user says "forget this", remove the relevant entry from the memory file.

## Project Context

I want to refactor this project: (https://github.com/FuzzyBootsDK/3D-Filament-Tracker-and-Cost-Calculator)
I want you to use this file esp32_s3_filament_tracker_port_plan_api_complete.md as a guide for how to refactor the project, and to use the same file structure as outlined in that file. I want you to read that file and understand it before starting to refactor the project. I want you to ask me any questions you have about the project or the file before starting to refactor. I want you to make suggestions on how to improve the project based on the file, and wait for my confirmation before implementing any suggestions. I want you to keep track of any decisions made during the refactoring process in the memory files, and update them accordingly.