# Klypr

Klypr est un sélecteur et lanceur d'applications ultra-rapide (*application launcher bar*) pour Windows, inspiré de Spotlight, Rofi et Wofi.

Conçu pour une productivité maximale au clavier, il offre un design moderne translucide (effet Acrylic / Mica) et une recherche instantanée d'applications, de raccourcis, d'URL et de thèmes.

---

## Raccourcis Clavier Principaux

| Raccourci | Action |
|---|---|
| `Ctrl` + `A` | **Afficher / Masquer** la barre de lancement rapide |
| `Alt` + `Entrée` | **Ouvrir le terminal** (configurable via `klypr.ini`, repli automatique intelligent) |
| `Alt` + `Shift` + `Q` | **Quitter** proprement Klypr |
| `Tab` | **Compléter** la commande ou l'alias sélectionné dans la barre de recherche |
| `Haut` / `Bas` | **Naviguer** dans les résultats de recherche |
| `Entrée` | **Valider** et exécuter l'action, l'application ou l'alias sélectionné |
| `Échap` | **Fermer** la barre du lanceur |

---

## Fonctionnalités

### 1. Recherche & Indexation instantanée
- Détection automatique et temps réel des applications installées (Menu Démarrer utilisateur et système, registre Windows `App Paths`, commandes système courantes).
- **Recherche Floue Intelligente (*Fuzzy Search*)** : trouvez instantanément vos applications par leurs initiales, acronymes ou abréviations (ex: `vsc` ➔ *Visual Studio Code*, `psh` ➔ *PowerShell*, `gc` ➔ *Google Chrome*, `wt` ➔ *Windows Terminal*).
- **Icônes natives d'applications** : extraction et affichage automatique en temps réel des véritables icônes pour chaque résultat (exécutables, raccourcis `.lnk`, dossiers, liens web et actions système).
- Saisie et ouverture directe d'adresses web (`http://`, `https://`, domaines `.com`, `.fr`...) ou de fichiers et dossiers locaux.

### 2. Système d'Alias Intelligent & Suggestions de Cibles
- **Gestion directe au clavier dans le lanceur :**
  - Tapez `:alias` pour afficher la liste de vos alias et les commandes disponibles.
  - Tapez `:alias <nom> <cible>` (ou `alias <nom> <cible>`) pour créer ou modifier un alias.
  - Tapez `:alias del <nom>` pour supprimer un alias existant.
- **Suggestions dynamiques de cibles :**
  - Dès la saisie de `:alias <nom> `, Klypr propose automatiquement les applications populaires installées.
  - Dès la frappe d'un début de cible (ex: `:alias nav chr`), Klypr filtre en temps réel les applications installées correspondantes (`Google Chrome`), les exécutables cibles (`chrome.exe`), les dossiers et les URLs web.
- **Complétion avec `Tab` :**
  - Appuyez sur `Tab` pour insérer la commande complète d'alias (`:alias <nom> <cible>`) dans le champ de saisie avant de valider.
- **Arguments dynamiques (`%s`) :**
  - Possibilité d'utiliser `%s` dans la cible pour injecter des paramètres (ex: recherche web `g = https://google.com/search?q=%s`).
  - Taper `g programmation c` dans Klypr effectue directement la recherche web encodée dans votre navigateur par défaut.

### 3. Commandes Système et d'Alimentation
Contrôlez rapidement votre PC et votre session Windows directement depuis la barre Klypr :

| Commande | Action | Description |
|---|---|---|
| `:lock` (ou `lock`) | **Verrouiller** | Verrouille instantanément la session Windows (`Win+L`) |
| `:sleep` (ou `sleep`, `veille`) | **Mise en veille** | Place le système en mode veille |
| `:hibernate` (ou `hibernate`) | **Veille prolongée** | Met l'ordinateur en veille prolongée |
| `:restart` (ou `reboot`, `redémarrer`) | **Redémarrer** | Redémarre l'ordinateur proprement et immédiatement |
| `:shutdown` (ou `éteindre`, `arrêt`) | **Arrêter** | Éteint le système immédiatement |
| `:logout` (ou `déconnexion`) | **Déconnexion** | Ferme la session utilisateur courante |
| `:emptybin` (ou `corbeille`, `trash`) | **Vider la corbeille** | Vide les corbeilles de tous les disques avec confirmation native |

- **Découverte rapide** : tapez simplement `:` pour lister toutes les commandes disponibles.
- **Complétion `Tab`** : tapez un début de commande (ex: `:l`, `:sl`) puis appuyez sur `Tab` pour compléter automatiquement.
- **Icônes système dédiées** : chaque action affiche l'icône système Windows correspondante (cadenas, alimentation, corbeille, redémarrage...).

### 4. Calculatrice Instantanée
Effectuez des calculs mathématiques en temps réel directement dans la barre de recherche sans ouvrir d'application tierce :
- **Opérations arithmétiques** : `12 * 45`, `12 x 45`, `100 / 4`, `(10 + 5) * 2`, `2^10`, `2 ** 8`, `10 % 3`, `10 mod 3`.
- **Nombres décimaux** : support natif du point et de la virgule décimale (`12.5 + 3.2` ou `12,5 + 3,2`).
- **Pourcentages intelligents** : `20% of 150` (30), `100 + 20%` (120), `100 - 20%` (80), `50 * 10%` (5).
- **Fonctions & constantes** : `sqrt(144)`, `abs(-42)`, `round(3.7)`, `floor(3.7)`, `ceil(3.2)`, `pow(2, 5)`, `sin(0)`, `cos(0)`, `log10(1000)`, `ln(e)`, `pi`, `e`.
- **Préfixe optionnel `=`** : force l'évaluation immédiate (ex: `= 42`, `= 2 + 2`).
- **Productivité maximale au clavier** :
  - `Entrée` : **Copie instantanément** le résultat dans le presse-papier Windows.
  - `Tab` : **Réutilise le résultat** directement dans le champ de saisie pour enchaîner les calculs.

### 5. Lancement Rapide du Terminal
- Raccourci global `Alt` + `Entrée` géré nativement via l'API noyau `RegisterHotKey` (aucun conflit, réactivité instantanée).
- Configurable dans `klypr.ini` via la clé `terminal` (ex: `wt.exe`, `powershell.exe`, `cmd.exe`, `bash.exe`...).
- Chaîne de repli automatique robuste :
  `terminal défini` ➔ `Windows Terminal (wt.exe)` ➔ `Package Windows Terminal AppX` ➔ `PowerShell` ➔ `CMD`.

### 6. Thèmes & Effets Visuels
- 5 thèmes intégrés :
  - **Système (Auto)** : s'adapte automatiquement au mode sombre / clair de Windows.
  - **Fluent Dark** : design sombre moderne translucide.
  - **Fluent Light** : design clair moderne translucide.
  - **Cyberpunk** : style terminal hacker vert néon sur fond sombre.
  - **Dracula** : thème violet et cyan pour développeurs.
- Effet Acrylic / Mica avec double-buffering GDI (rendu fluide et sans aucun scintillement).
- Changement de thème directement depuis le lanceur en tapant `thème`.

### 7. Lancement au Démarrage
- Synchronisation native avec le registre Windows (`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`).
- Activé par défaut, désactivable via `klypr.ini` ou directement en tapant `démarrage` dans Klypr.

---

## Configuration (`klypr.ini`)

Le fichier `klypr.ini` est situé dans le même répertoire que `klypr.exe` et permet de personnaliser l'ensemble du comportement :

```ini
[General]
# Lancement au démarrage de Windows (1 = activé, 0 = désactivé)
autostart = 1

# Terminal à ouvrir avec Alt+Entrée (ex: wt.exe, powershell.exe, cmd.exe, bash.exe)
# Repli automatique : wt.exe -> Package AppX -> PowerShell -> CMD
terminal = wt.exe

[Theme]
# Thème visuel : system, dark, light, cyberpunk, dracula
theme = system

# Opacité de la fenêtre en pourcentage (de 50 à 100)
opacity = 95

[Aliases]
# Définissez vos alias personnalisés ici.
# Format : nom = commande, URL ou chemin de fichier/dossier
# %s permet la substitution dynamique d'arguments lors de l'appel
g = https://www.google.com/search?q=%s
yt = https://www.youtube.com/results?search_query=%s
gh = https://github.com/search?q=%s
term = wt.exe
proj = explorer.exe C:\Projets
```

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
