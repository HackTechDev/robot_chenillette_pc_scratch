# BatMobile Scratch — programmation par blocs (C++ / Qt6)

Application de bureau (C++ / Qt6) pour **programmer les déplacements** du robot Zumo Shield **BatMobile** (`../robot_chenillette`) avec des blocs à emboîter, comme dans Scratch, puis exécuter le programme en Bluetooth classique (SPP/RFCOMM).

Même liaison, même appairage et même protocole que la télécommande [`../robot_chenillette_pc_qt6`](../robot_chenillette_pc_qt6).

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

1. **Connecter** le robot (en haut) : comme dans la télécommande, BatMobile est présélectionné, et appairé automatiquement s'il ne l'est pas encore. Sans robot, le programme s'exécute en **simulation** : les blocs s'allument, rien n'est envoyé.
2. **Glisser** les blocs de la palette (à gauche) vers la zone de programme et les emboîter sous **quand ▶ est cliqué**. Un trait blanc montre où le bloc va s'accrocher.
3. **▶** exécute le script qui commence par **quand ▶ est cliqué** (le plus haut s'il y en a plusieurs). **■** ou **Échap** arrête le programme et le robot.

Dans la zone de programme :

- clic sur un nombre : le modifier (virgule ou point) ;
- clic sur un script : l'exécuter seul ;
- glisser un bloc : il part avec les blocs qui le suivent ; le ramener sur la palette le supprime ;
- clic droit : **Dupliquer** (le bloc et les suivants) ou **Supprimer le bloc** (les suivants remontent).

Pendant l'exécution, le bloc en cours est entouré de jaune et les scripts ne peuvent pas être déplacés.

**Nouveau**, **Ouvrir…**, **Enregistrer…** (Ctrl+N / Ctrl+O / Ctrl+S) : les programmes sont des fichiers `.batmobile` (JSON).

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
