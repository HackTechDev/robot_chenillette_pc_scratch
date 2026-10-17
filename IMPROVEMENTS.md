# Améliorations à faire

Améliorations proposées le 17/10/2026 et pas encore réalisées. Les numéros sont ceux de la liste d'origine : les numéros manquants correspondent aux améliorations déjà faites (2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 14, 16, 17).

## Priorité

### 1. Essai avec le vrai robot et la souris

Tout a été vérifié uniquement par des tests hors écran (sans souris réelle, sans robot).

- Glisser-déposer à la souris : depuis la palette, entre scripts, vers la palette (suppression), aperçu fantôme.
- Connexion Bluetooth, exécution, pas à pas, arrêts de sécurité (■, Échap, déconnexion, fermeture, perte de liaison).
- **Calibrer la scène** : mesurer les déplacements du robot avec les exemples Carré et Aller-retour, puis ajuster `UnitsPerSpeed` et `DegreesPerSpeed` dans `src/stage.cpp`. Ajuster ensuite les durées des exemples (`examples/*.batmobile`) si les angles ne tombent pas juste.

## Éditeur

### 9. Commentaires

Notes jaunes à poser à côté des scripts (texte libre, déplaçables, enregistrées dans le fichier `.batmobile` et l'historique d'annulation).

## Nouveaux blocs

### 13. Mes blocs

Définir un bloc personnalisé (« faire un carré ») réutilisable dans le programme.

## Robot

### 15. Capteurs

Le Zumo a des capteurs de ligne et un accéléromètre, mais le firmware (`../robot_chenillette`) n'envoie rien au PC aujourd'hui. Il faudrait d'abord le modifier pour remonter des mesures, puis ajouter des blocs comme « si ligne détectée » et lire la liaison dans `Link` (aujourd'hui, une socket lisible signifie une liaison perdue).

## Technique

### 18. Tests automatiques et CI

Transformer les tests hors écran (exécution, emboîtement, enregistrement, annulation, zoom, aperçu fantôme, pas à pas, exemples) en cible `ctest` (Qt Test), et ajouter une CI GitHub Actions qui compile et lance les tests.

### 19. Code partagé avec la télécommande

`link`, `bluez`, `pairing`, `bt_pairing` et `config.h` sont recopiés depuis `../robot_chenillette_pc_qt6`. Un sous-module git ou une bibliothèque commune éviterait que les deux versions divergent.

### 20. Traduction

Passer les textes par `tr()` et Qt Linguist, si l'appli doit servir en dehors de la France.
