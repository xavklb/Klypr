# Klypr

Klypr est un gestionnaire de fenêtres par pavage dynamique (*dynamic tiling window manager*) pour Windows, inspiré de la philosophie et de la fluidité de Hyprland sous Linux.

Il organise automatiquement vos fenêtres sous la forme d'un arbre binaire (*Binary Space Partitioning* - BSP) pour maximiser l'espace écran et optimiser le flux de travail au clavier.

---

## Fonctionnalités

- **Agencement BSP dynamique** : subdivision automatique horizontale et verticale des fenêtres lors de l'ouverture et de la fermeture.
- **Filtrage intelligent** : prise en charge exclusive des fenêtres applicatives gérables (exclusion des barres d'outils, infobulles, fenêtres masquées ou sans titre).
- **Contrôle au clavier** : interception globale via des hooks Windows de bas niveau.
- **Raccourcis par défaut** :
  - `Ctrl` + `A` : Afficher / masquer le sélecteur d'application rapide style terminal.
  - `Alt` + `Entrée` : Lancer le terminal Windows (`wt.exe`).
  - `Alt` + `Shift` + `Q` : Quitter Klypr proprement.

---

## Structure du projet

```text
.
├── .github/
│   └── workflows/
│       └── build.yml
├── include/
│   ├── app.h          # Cycle de vie global de l'application
│   ├── config.h       # Configuration générale (espacements, bordures)
│   ├── event.h        # Hooks WinEvent (détection des ouvertures/fermetures)
│   ├── input.h        # Hooks clavier bas niveau WH_KEYBOARD_LL
│   ├── launcher.h     # Sélecteur d'application style terminal
│   ├── layout.h       # Définition de l'arbre BSP et agencement
│   └── window.h       # Filtrage et détection des fenêtres gérables
├── src/
│   ├── app.c
│   ├── config.c
│   ├── event.c
│   ├── input.c
│   ├── launcher.c
│   ├── layout.c
│   ├── main.c         # Point d'entrée WinMain
│   └── window.c
├── .gitignore
├── CMakeLists.txt
├── Makefile
└── README.md
```

---

## Prérequis

- **Système d'exploitation** : Windows 10 / 11 (64 bits recommandé).
- **Compilateur C** : standard C11 compatible :
  - MinGW-w64 (GCC ou Clang)
  - Microsoft Visual C++ (MSVC / Visual Studio 2019 ou supérieur)
- **Système de génération** :
  - [CMake](https://cmake.org/) (version 3.16 ou supérieure)
  - ou [GNU Make](https://www.gnu.org/software/make/) (avec environnement MinGW/MSYS2)

---

## Compilation

### Avec CMake (Recommandé)

#### Avec MSVC (Visual Studio) :
```powershell
cmake -B build -S .
cmake --build build --config Release
```
L'exécutable `klypr.exe` se trouvera dans `build/Release/klypr.exe`.

#### Avec MinGW :
```powershell
cmake -B build -S . -G "MinGW Makefiles"
cmake --build build
```
L'exécutable `klypr.exe` se trouvera dans `build/klypr.exe`.

---

### Avec Make (MinGW / GCC)

```powershell
make
```
L'exécutable `klypr.exe` sera généré à la racine du projet.

Pour nettoyer les fichiers intermédiaires et l'exécutable :
```powershell
make clean
```

---

## Exécution

Lancez simplement le binaire généré :

```powershell
.\klypr.exe
```

Klypr tourne en arrière-plan sans console. Utilisez `Alt` + `Shift` + `Q` pour l'arrêter.
