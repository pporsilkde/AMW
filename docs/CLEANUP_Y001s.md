# Y001s source cleanup

Y001s is a source-distribution cleanup, not a deletion of engine functionality.

Removed from the root/package:
- embedded `.git` database;
- old `X0xx` patch/review/note files and logic harnesses whose results are already integrated;
- old changed/deleted-file lists and SHA manifests tied to historical cumulative archives;
- legacy Travis, GitLab CI, AppVeyor and ReadTheDocs root configs not used by the current release workflow.

Preserved:
- engine and launcher source;
- `.github/workflows/windows.yml`;
- OpenMW AUTHORS/LICENSE and upstream history under `docs/upstream/`;
- VFS, shaders, MyGUI resources and localisation;
- optional EncoreMP content under `extras/EncoreMP`;
- experimental platform/render code that is still compiled or referenced by the project.
