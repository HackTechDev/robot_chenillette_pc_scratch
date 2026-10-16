# BatMobile Scratch — programmation par blocs (C++ / Qt6)

Application de bureau (C++ / Qt6) pour **programmer les déplacements** du robot Zumo Shield **BatMobile** (`../robot_chenillette`) avec des blocs à emboîter, comme dans Scratch, puis exécuter le programme en Bluetooth classique (SPP/RFCOMM).

Même liaison, même appairage et même protocole que la télécommande [`../robot_chenillette_pc_qt6`](../robot_chenillette_pc_qt6).

Dépôt : [github.com/HackTechDev/robot_chenillette_pc_scratch](https://github.com/HackTechDev/robot_chenillette_pc_scratch)

## Prérequis

- Linux avec BlueZ (service `bluetooth` actif) et un adaptateur Bluetooth
- Qt ≥ 6.4 (modules Widgets et DBus), CMake ≥ 3.19, un compilateur C++20 :
  `sudo apt install qt6-base-dev cmake g++`

## Compiler et lancer

```bash
./build.sh           # → build/batmobile_scratch et build/bt_pairing
./launchScratch.sh   # compile au premier lancement, puis lance l'appli
./runPairing.sh [--repair] [--timeout N]   # appairage seul (voir la télécommande)
```

## Utilisation

1. **Connecter** le robot (en haut) : comme dans la télécommande, BatMobile est présélectionné, et appairé automatiquement s'il ne l'est pas encore. Sans robot, le programme s'exécute quand même : seule la scène bouge.
2. **Glisser** les blocs de la palette (à gauche) vers la zone de programme et les emboîter sous **quand ▶ est cliqué**. Un trait blanc montre où le bloc va s'accrocher.
3. **▶** exécute le script qui commence par **quand ▶ est cliqué** (le plus haut s'il y en a plusieurs). **■** ou **Échap** arrête le programme et le robot.

Dans la zone de programme :

- clic sur un nombre : le modifier (virgule ou point) ; **molette** sur un nombre : l'augmenter ou le diminuer (durées : 0,1 s par cran jusqu'à 2 s, puis 0,5 s ; nombres de tours et vitesse : 1 par cran) ;
- clic sur un script : l'exécuter seul ;
- glisser un bloc : il part avec les blocs qui le suivent ; le ramener sur la palette le supprime ;
- clic droit sur un bloc : **Dupliquer** (le bloc et les suivants) ou **Supprimer le bloc** (les suivants remontent) ;
- clic droit sur le fond : **Ranger les scripts** (en colonne à gauche, dans l'ordre de haut en bas) ou **Tout supprimer** (annulables avec ↶) ;
- glisser le fond (ou bouton du milieu) : déplacer la vue ;
- **zoom** de 50 à 200 % : Ctrl+molette (autour de la souris), boutons **− / 100 % / +** en bas, Ctrl++ / Ctrl+− / Ctrl+0.

Pendant l'exécution, le bloc en cours est entouré de jaune et les scripts ne peuvent pas être déplacés.

**↶ / ↷** (Ctrl+Z / Ctrl+Y ou Ctrl+Maj+Z) : annuler ou rétablir la dernière modification (ajout, déplacement, suppression, valeur, rangement), jusqu'à 200 étapes ; plusieurs crans de molette d'affilée comptent pour une seule étape. Indisponible pendant l'exécution.

**Nouveau**, **Ouvrir…**, **Enregistrer…** (Ctrl+N / Ctrl+O / Ctrl+S) : les programmes sont des fichiers `.batmobile` (JSON).

**Sauvegarde automatique** : le programme en cours, enregistré ou non, est sauvegardé une seconde après chaque modification et à la fermeture, dans `~/.local/share/batmobile_scratch/autosave.batmobile`. Il est rouvert au lancement suivant (même après un plantage), avec son nom de fichier et l'astérisque s'il n'était pas enregistré. Quitter ne demande donc plus d'enregistrer.

## Scène (simulation)

À droite, le robot vu de dessus suit les commandes du programme, robot connecté ou non : déplacements avec la trace du parcours, ouverture de la pince, jauge de la hauteur de la tête. **⟲ Replacer** le remet au centre et efface la trace. Il bute sur les bords de la scène.

Les vitesses sont des approximations (`stage.cpp`) : à la vitesse par défaut, 100 unités/s en ligne droite (la scène fait 480 × 360) et 180°/s en pivot, donc **pivoter 0,5 s ≈ un quart de tour**. Le vrai robot dépend du sol et de la batterie : pour que la scène lui ressemble, mesurer ses déplacements et ajuster `UnitsPerSpeed` et `DegreesPerSpeed`.

## Blocs

| Bloc | Envoyé au robot |
|------|-----------------|
| quand ▶ est cliqué | début du script |
| avancer / reculer pendant N s | `z` / `s`, attente N s, puis stop |
| pivoter ↺ / ↻ pendant N s | `q` / `d`, attente, stop |
| fermer / ouvrir la pince pendant N s | `v` / `c`, attente, stop |
| lever / baisser la tête pendant N s | `w` / `x`, attente, stop |
| vitesse N (0 à 9) | chiffre `N` → vitesse `(N + 1) × 40` |
| stop | espace |
| attendre N s | rien |
| répéter N fois { … } | boucle |
| répéter indéfiniment { … } | boucle sans fin (jusqu'à ■) ; aucun bloc ne peut la suivre |

Chaque exécution commence par remettre la vitesse par défaut (`4` → 200).

## Sécurité

- **■**, **Échap**, **Déconnecter**, la fermeture de l'appli et la perte de liaison arrêtent le programme et envoient un stop ;
- un bloc « pendant N s » envoie toujours un stop à la fin de sa durée (le firmware n'a pas de keep-alive) ;
- une boucle sans attente rend la main à l'interface à chaque tour : ■ reste utilisable.

Pour le dépannage Bluetooth, voir le README de la télécommande.
