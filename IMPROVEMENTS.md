# Améliorations à faire

Améliorations pas encore réalisées. Les numéros 1 à 20 viennent de la liste du 17/10/2026 (les numéros manquants sont déjà faits : 2 à 14, 16, 17) ; les numéros 21 à 36 ont été proposés le 18/10/2026 (faits : 21, 22, 23).

Ordre conseillé : **1** (tout en dépend), puis **25** et **26**, et **28** si l'essai réel montre une dérive.

## Priorité

### 1. Essai avec le vrai robot et la souris

Tout a été vérifié uniquement par des tests hors écran (sans souris réelle, sans robot).

- Glisser-déposer à la souris : depuis la palette, entre scripts, vers la palette (suppression), aperçu fantôme.
- Connexion Bluetooth, exécution, pas à pas, arrêts de sécurité (■, Échap, déconnexion, fermeture, perte de liaison).
- **Calibrer le robot** avec l'assistant (⚙ Calibrer… sous la scène) : avancer, reculer, pivoter ↺ et ↻, à quelques vitesses (par exemple 2, 4, 6) ; vérifier ensuite avec les exemples Carré et Aller-retour (en cm et degrés) que le robot revient à son point de départ. Contrôler aussi la dérive latérale en ligne droite (chenilles inégales), que le calibrage ne corrige pas (voir 28).

## Programmation

### 24. Bloc « jouer un son »

Sons joués par le PC (bip, klaxon…). Le buzzer du robot n'est pas utilisable : il entre en conflit avec le Timer 2 des servos.

## Scène et pédagogie

### 25. Obstacles et objets sur la scène

Murs, balle à attraper, zone d'arrivée, posés à la souris sur le terrain (en cm) et enregistrés avec le programme. Le robot simulé s'arrête contre un mur ; la pince peut saisir la balle et la déplacer.

### 26. Défis guidés

« Amène le robot sur l'étoile », « Fais le tour du carré sans toucher les murs » : défis vérifiés dans la simulation (s'appuie sur 25), classés par niveau, avec des blocs imposés pour les premiers. Permet de travailler en classe même avec un seul robot.

### 27. Mode présentation

Scène en plein écran et blocs agrandis, pour le vidéoprojecteur.

## Robot

### 15. Capteurs

Le Zumo a des capteurs de ligne et un accéléromètre, mais le firmware (`../robot_chenillette`) n'envoie rien au PC aujourd'hui. Il faudrait d'abord le modifier pour remonter des mesures, puis ajouter des conditions comme ⟨ligne détectée⟩ pour les blocs « si » et lire la liaison dans `Link` (aujourd'hui, une socket lisible signifie une liaison perdue).

### 28. Correction de la dérive

Réglage « trim » gauche / droite que le calibrage ne corrige pas : nouvelle commande du firmware pour ralentir légèrement une chenille, et essai dans l'assistant (avancer 50 cm, mesurer l'écart latéral).

### 29. Niveau des piles

Le firmware enverrait la tension des piles ; l'appli l'afficherait et préviendrait quand le calibrage n'est plus fiable. Premier pas vers des données qui remontent du robot (voir 15).

### 30. Reconnexion automatique

Après une perte de liaison, retenter la connexion au même robot (avec un message, sans relancer le programme).

### 31. Programme téléversé dans le robot

Le firmware interpréterait les blocs lui-même : plus de délai Bluetooth, et le robot fonctionne sans PC une fois lancé. Gros chantier (format compact, interpréteur dans la mémoire de l'Arduino).

## Confort

### 32. Copier / coller

Ctrl+C / Ctrl+V de scripts, y compris d'un programme à un autre (presse-papiers en JSON), et sélection de plusieurs scripts.

### 33. Palette

Catégories repliables et champ de recherche : la palette devient longue (Mouvement compte 14 blocs).

### 34. Commentaires attachés à un bloc

Commentaire rattaché à un bloc, qui suit son script quand on le déplace (aujourd'hui les notes sont libres).

### 35. Fichiers récents et export en image

Menu des derniers programmes ouverts, et export d'un programme en image PNG pour l'imprimer.

## Technique

### 18. Tests automatiques et CI

Transformer les tests hors écran (exécution, emboîtement, enregistrement, annulation, zoom, aperçu fantôme, pas à pas, exemples, calibrage) en cible `ctest` (Qt Test), et ajouter une CI GitHub Actions qui compile et lance les tests.

### 19. Code partagé avec la télécommande

`link`, `bluez`, `pairing`, `bt_pairing` et `config.h` sont recopiés depuis `../robot_chenillette_pc_qt6`. Un sous-module git ou une bibliothèque commune éviterait que les deux versions divergent.

### 20. Traduction

Passer les textes par `tr()` et Qt Linguist, si l'appli doit servir en dehors de la France.

### 36. Installation

AppImage ou paquet .deb, avec icône et entrée dans le menu des applications.
