# Klypr

Klypr est un sélecteur et lanceur d'applications ultra-rapide (*application launcher bar*) pour Windows, inspiré de Spotlight, Rofi et Wofi.

Conçu pour une productivité maximale au clavier, il offre un design moderne translucide (effet Acrylic / Mica) et une recherche instantanée d'applications, de raccourcis, d'URL et de thèmes.

---

## Fonctionnalités

- **Recherche & Indexation instantanée** : détection automatique des applications installées (Menu Démarrer, registre `App Paths`, commandes système courantes).
- **Navigation & URLs directes** : saisie directe d'adresses web ou de commandes système.
- **Thèmes & Personnalisation** : support de plusieurs thèmes intégrés (`system`, `dark`, `light`, `cyberpunk`, `dracula`) et opacité réglable via fichier INI (`klypr.ini`) ou directement dans le lanceur.
- **Lancement au démarrage** : synchronisation automatique avec le démarrage de Windows (`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`), configurable dans `klypr.ini` ou via la barre.
- **Contrôle au clavier** : interception globale via des hooks Windows de bas niveau.
- **Raccourcis par défaut** :
  - `Ctrl` + `A` : Afficher / masquer la barre de lancement rapide.
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
│   ├── config.h       # Configuration générale (thèmes, opacité)
│   ├── input.h        # Hooks clavier bas niveau WH_KEYBOARD_LL
│   └── launcher.h     # Sélecteur d'application style terminal / barre rapide
├── src/
│   ├── app.c
│   ├── config.c
│   ├── input.c
│   ├── launcher.c
│   └── main.c         # Point d'entrée WinMain
├── .gitignore
├── CMakeLists.txt
├── Makefile
├── klypr.ini
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
