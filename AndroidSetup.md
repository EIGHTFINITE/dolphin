# How to Set Up an Android Development Environment

If you'd like to contribute to the Android project, but do not currently have a development environment setup, follow the instructions in this guide.

## Prerequisites

* A Linux VM or host, or a Mac.
* JDK 7 for your platform.
* CMake
* [Android NDK](https://developer.android.com/tools/sdk/ndk/index.html) 
* [Android Studio](http://developer.android.com/tools/studio/index.html) **OR**
* [Android SDK Tools](http://developer.android.com/sdk/index.html#Other) (for command-line usage)

If you downloaded Android Studio, extract it and then see [Setting up Android Studio](#setting-up-android-studio).

If you instead chose to download the commoand-line SDK tools, see [Setting up the SDK Tools](#setting-up-the-sdk-tools).

## Setting up Android Studio

1. Launch Android Studio, which will start a first-launch wizard.
2. Choose a custom installation.
3. If offered a choice of themes, select your preference.
4. When offered a choice of components, uncheck the "Android Virtual Device" option. ![Android Studio Components][components]
5. Accept all licenses, and click Finish. Android Studio will download the SDK Tools package automatically. (Ubuntu users, if you get an error running the `mksdcard` tool, make sure the `lib32stdc++6` package is installed.)
6. At the Android Studio welcome screen, click "Configure", then "SDK Manager".
7. Use the SDK Manager to get necessary dependencies, as described in [Getting Dependencies](#getting-dependencies).
8. When done, follow the steps in [Readme.md](Readme.md#installation-on-android) to compile and deploy the application.

## Setting up the SDK Tools

1. In `Source/Android`, create a file called `local.properties`.
2. Add a single line: `sdk.dir=<sdk-path>`, where `<sdk-path>` is the path where you extracted the SDK Tools package.
3. Follow the steps in [Readme.md](Readme.md#installation-on-android) to compile and deploy the application.

## Executing Gradle Tasks

In Android Studio, you can find a list of possible Gradle tasks in a tray at the top right of the screen:

![Gradle Tasks][gradle]

Double clicking any of these tasks will execute it, and also add it to a short list in the main toolbar:

![Gradle Task Shortcuts][shortcut]

Clicking the green triangle next to this list will execute the currently selected task.

For command-line users, any task may be executed with `Source/Android/gradlew <task-name>`.

## Getting Dependencies

Most dependencies for the Android project are supplied by Gradle automatically. However, Android platform libraries (and a few Google-supplied supplementary libraries) must be downloaded through the Android package manager.

1. Launch the Android SDK Manager from the commandline by executing `<sdk-path>/tools/android`, or by clicking on its icon in Android Studio's main toolbar:
![Android Studio Package Icon][package-icon]
2. At the bottom of the window, click "Deselect All", and then "Updates".
3. Install or update the following packages:

* SDK Platform, under "Android 5.0.1 (API 21)". This will allow compiling apps that target Lollipop.
* Android Support Repository
* Android Support Library
* Google Repository

In the future, if the project targets a newer version of Android, or use newer versions of the tools/build-tools packages, it will be necessary to use this tool to download updates.

[components]: http://i.imgur.com/Oo1Fs93.png
[package-icon]: http://i.imgur.com/NUpkAH8.png
[gradle]: http://i.imgur.com/dXIH6o3.png
[shortcut]: http://i.imgur.com/eCWP4Yy.png