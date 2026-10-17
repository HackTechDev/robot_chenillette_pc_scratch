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
2. **Glisser** les blocs de la palette (à gauche ; clic sur le titre d'une catégorie pour la replier ou la déplier, champ **🔍 Rechercher un bloc** pour filtrer par texte, sans tenir compte des accents ni des majuscules) vers la zone de programme et les emboîter sous **quand ▶ est cliqué**. Pendant le glisser, un aperçu gris montre où la pile va s'accrocher (les blocs suivants se décalent pour lui faire de la place).
3. **▶** exécute le script qui commence par **quand ▶ est cliqué** (le plus haut s'il y en a plusieurs). **■** ou **Échap** arrête le programme et le robot.
4. **⏭ Pas à pas** (F10) : le programme s'arrête avant chaque bloc d'action, entouré de jaune, robot à l'arrêt. **⏭** exécute ce bloc puis s'arrête avant le suivant, **▶** (F5) continue normalement, **■** arrête. Les blocs « répéter » sont traversés sans arrêt.

Dans la zone de programme :

- clic sur un nombre : le modifier (virgule ou point ; durées au centième, ex. 0,25 s) ; **molette** sur un nombre : l'augmenter ou le diminuer (durées : 0,1 s par cran jusqu'à 2 s, puis 0,5 s ; nombres de tours et vitesse : 1 par cran) ;
- clic sur un script : l'exécuter seul, comme dans Scratch ; **robot connecté, il faut un double-clic** (un clic involontaire ne fait pas bouger le robot) ;
- glisser un bloc : il part avec les blocs qui le suivent ; le ramener sur la palette le supprime ;
- **sélection** : un clic sélectionne un bloc et ceux qui le suivent (halo bleu), **Ctrl+clic** en ajoute ou en retire, **Ctrl+A** sélectionne tous les scripts, un clic sur le fond désélectionne ; **Ctrl+C** / **Ctrl+X** / **Ctrl+V** copient, coupent et collent la sélection (y compris d'un programme à un autre : les variables suivent), le collage se fait sous la souris ; **Suppr** supprime la sélection (annulable avec ↶) ;
- clic droit sur un bloc : **Dupliquer** (le bloc et les suivants), **Copier** ou **Supprimer le bloc** (les suivants remontent) ;
- clic droit sur le fond : **Ajouter un commentaire**, **Coller**, **Ranger les scripts** (en colonne à gauche, dans l'ordre de haut en bas) ou **Tout supprimer** (scripts et commentaires ; annulables avec ↶) ;
- **commentaires** (notes jaunes) : clic droit sur le fond → **Ajouter un commentaire** (note libre), ou sur un bloc → **Ajouter un commentaire** (note **attachée** à ce bloc, à sa droite, reliée par un trait : elle suit son script quand on le déplace et disparaît avec le bloc s'il est supprimé) ; clic sur la note pour écrire, clic ailleurs pour valider ; glisser la note pour la déplacer (une note attachée garde sa place par rapport au bloc), son coin bas-droit pour la redimensionner ; clic droit → **Détacher du bloc** ou **Supprimer le commentaire**. Les notes sont enregistrées avec le programme (attaches comprises) et s'annulent avec ↶ ; copier / coller et Dupliquer ne copient pas les notes ;
- glisser le fond (ou bouton du milieu) : déplacer la vue ;
- **zoom** de 50 à 200 % : **molette** (autour de la souris ; sur un nombre, la molette change le nombre, Ctrl+molette zoome quand même), boutons **− / 100 % / +** en bas, Ctrl++ / Ctrl+− / Ctrl+0 ; **Maj+molette** fait défiler la vue.

Pendant l'exécution, le bloc en cours est entouré de jaune et les scripts ne peuvent pas être déplacés.

**↶ / ↷** (Ctrl+Z / Ctrl+Y ou Ctrl+Maj+Z) : annuler ou rétablir la dernière modification (ajout, déplacement, suppression, valeur, rangement), jusqu'à 200 étapes ; plusieurs crans de molette d'affilée comptent pour une seule étape. Indisponible pendant l'exécution.

**Nouveau**, **Ouvrir…**, **Enregistrer…** (Ctrl+N / Ctrl+O / Ctrl+S) : les programmes sont des fichiers `.batmobile` (JSON).

**Plus ▾** : **Fichiers récents** (les 8 derniers programmes ouverts ou enregistrés ; un fichier disparu est retiré de la liste) et **Exporter en image (PNG)…** (tous les scripts et commentaires sur fond blanc, en double résolution, pour imprimer ou partager).

**Exemples ▾** : programmes tout faits, à ouvrir puis modifier (Enregistrer… en fait une copie) :

| Exemple | Ce qu'il fait |
|---------|---------------|
| Carré | répéter 4 fois : avancer de 20 cm, pivoter ↻ de 90° |
| Aller-retour | avancer de 30 cm, demi-tour (180°), revenir, demi-tour |
| Slalom | zigzag de trois virages (pivots de 45°) |
| Attraper un objet | ouvrir la pince, avancer de 20 cm, fermer, lever la tête, reculer de 20 cm, reposer |
| Danse | vitesse 6, répéter indéfiniment : gauche, droite, tête haut, tête bas (■ pour arrêter) |

Les exemples utilisent les blocs en cm et en degrés : une fois le robot calibré (voir **Calibrage**), ils tombent juste sur le vrai robot aussi.

**Sauvegarde automatique** : le programme en cours, enregistré ou non, est sauvegardé une seconde après chaque modification et à la fermeture, dans `~/.local/share/batmobile_scratch/autosave.batmobile`. Il est rouvert au lancement suivant (même après un plantage), avec son nom de fichier et l'astérisque s'il n'était pas enregistré. Quitter ne demande donc plus d'enregistrer.

## Scène (simulation)

À droite, le robot vu de dessus suit les commandes du programme, robot connecté ou non : déplacements avec la trace du parcours, ouverture de la pince, jauge de la hauteur de la tête. Pendant un bloc « ouvrir / fermer la pince », une **loupe** en bas à gauche montre la pince en grand qui s'ouvre ou se ferme (« s'ouvre… », « se ferme… », puis « ouverte », « fermée » ou « ouverte à N % ») ; elle s'efface une seconde après. **⟲ Replacer** le remet au centre et efface la trace. Il bute sur les bords de la scène.

**Zoom de la scène** : la **molette** zoome autour de la souris (jusqu'à ×6), **glisser** déplace la vue zoomée, **double-clic** revient à 100 % ; le terrain reste toujours visible en entier dans la vue. Les variables, la jauge de la tête et la loupe de la pince gardent leur taille. Fonctionne aussi dans la fenêtre séparée.

**⧉ Fenêtre** place la scène dans une fenêtre séparée, redimensionnable (par exemple sur un second écran ou le vidéoprojecteur) ; **F11** l'y met en plein écran. Dans cette fenêtre, **Échap** arrête le programme (et quitte le plein écran), **F5** / **F10** lancent ou avancent pas à pas, et les touches des scripts « quand la touche … » fonctionnent. Fermer la fenêtre (ou **⧉ Rattacher**) remet la scène à sa place ; l'appli se souvient de ce choix et de la position de la fenêtre au lancement suivant.

Le terrain mesure **160 × 120 cm** (carreaux de 10 cm) et le robot y est dessiné à l'échelle (~12 cm). La scène utilise le **calibrage** : vitesse en cm/s, démarrage progressif en ligne droite (comme le firmware, qui accélère sur ~0,4 s à la vitesse 200), pivots en °/s. Sans calibrage, valeurs par défaut : 20 cm/s et 180°/s à la vitesse 4.

## Calibrage

Pour que « avancer de 20 cm » fasse vraiment 20 cm, le robot doit être mesuré : **⚙ Calibrer…** sous la scène ouvre l'assistant (robot connecté, sur le sol habituel, piles chargées).

- **Avancer / reculer** : choisir la vitesse, aligner l'avant du robot sur le 0 d'un mètre ruban, **Lancer 1 s**, noter la distance ; replacer, **Lancer 3 s**, noter ; **Calculer et enregistrer**. L'assistant en déduit la vitesse établie (cm/s) et le temps perdu au démarrage (la rampe du firmware) : distance ≈ vitesse × (durée − départ).
- **Pivoter ↺ / ↻** : poser le robot sur une feuille avec un trait sous l'avant, ajuster la durée et **Lancer** jusqu'à ce que le robot fasse exactement un tour, puis **Enregistrer** (°/s = 360 / durée). Mesurer un tour complet est bien plus précis qu'un quart de tour.

Quelques vitesses suffisent (par exemple 2, 4 et 6) : les autres sont interpolées entre deux mesures, ou proportionnelles à la vitesse moteur au-delà. Le tableau de l'assistant montre, pour chaque vitesse, la valeur utilisée (mesurée ou déduite) ; **Valeurs par défaut pour ce mouvement** efface les mesures. Le calibrage est enregistré dans `~/.local/share/batmobile_scratch/calibration.json` (commun à tous les programmes) et sert aux blocs en cm / degrés et à la scène. Refaire les mesures si le sol ou les piles changent beaucoup.

## Blocs

| Bloc | Envoyé au robot |
|------|-----------------|
| quand ▶ est cliqué | début du script |
| quand la touche [espace ▾] est pressée | début d'un script lancé au clavier |
| avancer / reculer de N cm | `z` / `s` pendant la durée calculée avec le calibrage (à la vitesse en cours), puis stop |
| pivoter ↺ / ↻ de N ° | `q` / `d` pendant la durée calculée avec le calibrage, puis stop |
| avancer / reculer pendant N s | `z` / `s`, attente N s, puis stop |
| pivoter ↺ / ↻ pendant N s | `q` / `d`, attente, stop |
| fermer / ouvrir la pince pendant N s | `v` / `c`, attente, stop |
| lever / baisser la tête pendant N s | `w` / `x`, attente, stop |
| démarrer avancer / reculer / pivoter ↺ / ↻ | `z` / `s` / `q` / `d`, sans attente : le robot continue jusqu'au prochain stop ou mouvement |
| vitesse N (0 à 9) | chiffre `N` → vitesse `(N + 1) × 40` |
| stop | espace |
| dire [texte] / dire [texte] pendant N s | bulle au-dessus du robot dans la scène (rien n'est envoyé au robot) |
| attendre N s | rien |
| répéter N fois { … } | boucle |
| répéter indéfiniment { … } | boucle sans fin (jusqu'à ■) ; aucun bloc ne peut la suivre |
| si ⟨condition⟩ alors { … } / si ⟨condition⟩ alors { … } sinon { … } | exécute un corps selon la condition |
| répéter jusqu'à ⟨condition⟩ { … } | répète le corps tant que la condition est fausse (vérifiée avant chaque tour) |
| ⟨a < b⟩, ⟨a = b⟩, ⟨a > b⟩, ⟨… et …⟩, ⟨… ou …⟩, ⟨non …⟩ | **conditions** (blocs hexagonaux, vrai / faux) |
| mettre [var ▾] à N / ajouter N à [var ▾] | change une variable |
| définir [nom] (paramètres) / [nom] (valeurs) / (paramètre) | **mes blocs** : définition d'un bloc personnalisé, son appel, et la valeur d'un paramètre dans la définition |
| (var), (a + b), (a − b), (a × b), (a ÷ b), (nombre aléatoire entre a et b) | **reporters** (blocs arrondis) : une valeur, à glisser dans n'importe quelle entrée numérique |

Chaque exécution commence par remettre la vitesse par défaut (`4` → 200), sauf celle d'un script de touche.

**Variables et opérateurs** : **Créer une variable** (section Variables de la palette) ajoute un reporter à son nom, et les blocs « mettre … à » / « ajouter … à ». Les reporters (variables et opérateurs verts) se glissent dans une entrée — durée, nombre de tours, vitesse, ou entrée d'un autre opérateur — pour calculer une valeur pendant l'exécution, par exemple une spirale : « répéter 10 fois : ajouter 0,1 à durée, avancer pendant (durée) s, pivoter ↻ 0,5 s ». Le nom de variable se choisit dans la liste ▾ du bloc. Les valeurs calculées sont bornées à l'usage (durée 0–60 s, vitesse 0–9…) ; une division par zéro donne 0 ; un nombre aléatoire entre deux entiers est entier. Les variables gardent leur valeur d'une exécution à l'autre (comme dans Scratch) et s'affichent en haut à gauche de la scène. Un clic sur un reporter seul affiche sa valeur. Clic droit sur une variable de la palette : la supprimer (si aucun bloc ne l'utilise).

**Dire** : les blocs **dire [Bonjour !]** et **dire [Bonjour !] pendant 2 s** (catégorie Apparence) affichent une bulle au-dessus du robot dans la scène. Clic sur le texte pour le changer (40 caractères au plus), ou glisser un reporter dans la case pour afficher une valeur (variable, calcul, ⟨condition⟩ → vrai / faux). La bulle de « dire » reste jusqu'au « dire » suivant ; celle de « dire … pendant » s'efface à la fin de la durée ; ■ et une nouvelle exécution les effacent.

**Conditions** : les blocs hexagonaux (vert, catégorie Opérateurs) donnent vrai ou faux et ne se placent que dans une case de condition (hexagone en creux) : celle de « si », « répéter jusqu'à », « et », « ou », « non ». Les comparaisons acceptent des nombres ou des reporters, par exemple ⟨(distance) > 100⟩. Une case de condition vide vaut faux. Un clic sur une condition seule affiche « vrai » ou « faux ».

**Mes blocs** : **Créer un bloc** (section Mes blocs de la palette) demande un nom, par exemple « faire un carré », et jusqu'à deux **paramètres** facultatifs, par exemple « côté » ; il pose un chapeau rose **définir faire un carré (côté)** sous les scripts ; on empile dessous ce que fait le bloc. La palette propose alors le bloc **faire un carré**, à utiliser dans n'importe quel script, y compris dans sa propre définition. Un clic sur le script « définir … » l'exécute pour l'essayer. Si deux définitions portent le même nom, la plus haute est utilisée ; supprimer le chapeau « définir » retire le bloc de la palette (les appels restants ne font rien). Un bloc qui s'appelle lui-même sans fin (plus de 1000 appels imbriqués) arrête le programme avec un message. Chaque paramètre devient une case à remplir dans l'appel (« faire un carré (20) », nombre ou reporter) et un reporter rose **(côté)** dans la palette, à utiliser dans la définition : il vaut la valeur passée par l'appel en cours (chaque appel a la sienne, même en récursion ; hors d'un appel, 0). Les paramètres ne se modifient plus après la création du bloc. En pas à pas, l'arrêt se fait sur les blocs de la définition, pas sur l'appel.

**Scripts de touche** : le bloc **quand la touche … est pressée** (touche choisie dans sa liste : espace, flèches, lettres, chiffres ; molette pour la changer) lance son script quand on appuie sur la touche, pour piloter le robot au clavier. Une nouvelle touche remplace le script de touche en cours ; pendant un programme ▶ ou le pas à pas, les touches sont ignorées. Un appui maintenu ne relance pas le script. Les touches ne sont pas prises pendant la saisie d'un nombre.

Les blocs **démarrer …** permettent des mouvements continus, par exemple « démarrer avancer, attendre 2 s, démarrer pivoter ↻, attendre 0,5 s, stop » ; changer la vitesse pendant le mouvement ne l'arrête pas.

## Sécurité

- **■**, **Échap**, **Déconnecter**, la fermeture de l'appli et la perte de liaison arrêtent le programme et envoient un stop ;
- un bloc « pendant N s » envoie toujours un stop à la fin de sa durée (le firmware n'a pas de keep-alive) ;
- après un bloc **démarrer …**, un stop est envoyé à la fin du programme et à chaque pause du pas à pas ;
- une boucle sans attente rend la main à l'interface à chaque tour : ■ reste utilisable.

Pour le dépannage Bluetooth, voir le README de la télécommande.
