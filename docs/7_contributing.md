# Contributing

> *Prerequisites*: You have made it through the documentation and are wondering what you can do to make the robot dogs bark even louder.

## Getting Started

1. Fork the Repository
    - Click the Fork button at the top right corner to create a copy of this repository on your account.

1. Clone Your Fork
    - On your GitHub fork, click the "Code" button, copy the URL, and run `git clone --recurse-submodules [URL]` in your terminal. The firmware needs the nanopb submodule.

1. Create a Branch
    - Navigate into the repository directory on your computer.
    - Create a new branch using git checkout -b your-branch-name.

## Making Changes

1. Make Your Changes
    - Open the project in your editor/IDE and make your changes or additions.
    - Format web app code with Prettier (`pnpm format` in `app/`) and C++ with ClangFormat (`esp32/.clang-format`).

1. Run the checks
    - [Developing](6_developing.md) lists the commands for each part of the repository. The same checks run in CI, and a pull request needs them to pass.

1. Commit Your Changes
    - After making changes, stage them using git add .
    - Commit the changes with a single-line message: a gitmoji followed by a verb in the third person, as in the output of `git log`.

## Submitting Contributions

1. Push to Your Fork
    - Push your branch changes to your fork with git push origin your-branch-name.
1. Create a Pull Request
    - Go to the original repository on GitHub.
    - You'll see a "Compare & pull request" button. Click it, review your changes, then submit your pull request against `master` with a clear description of the enhancements or fixes.

## After Submission

- Wait for the project maintainers to review your pull request. They might suggest some changes. Keep an eye on your GitHub notifications for feedback or merge information.

Thank you for contributing!
