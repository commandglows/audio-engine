# Instructions du projet

Sois concis.

## Recherche Web

- Pour toute tâche nécessitant une recherche Web externe, utilise toujours l'API Parallel Search via `shipglows-parallel search` pour la découverte. Ne bascule pas silencieusement vers un autre fournisseur si Parallel est indisponible.
- Préfère un appel avec une à trois requêtes ciblées; le mode `fast` par défaut du CLI convient à la recherche multilingue. Les recherches sont envoyées à Parallel et peuvent consommer le quota du compte; n'inclus jamais de renseignements confidentiels ou personnels.
- Exécute la commande `shipglows-parallel` installée quand elle est disponible. Sinon, utilise `python C:\Users\Diane\ShipGlows\shipglows\tools\shipglows_parallel.py search ...`. Lis `C:\Users\Diane\ShipGlows\shipglows\shipglows_data\technical\parallel-web-search-cli.md` pour connaître son interface et ses limites actuelles.
- Pour la recherche Web personnelle partagée, exécute `doppler run --project dotfiles --config dev -- shipglows-parallel search ...` depuis n'importe quel dossier. Cette sélection explicite sert uniquement à l'outil de recherche; préserve le projet/config Doppler de chaque application. Utilise `PARALLEL_API_KEY` uniquement depuis cet environnement secret local approuvé. N'affiche, ne journalise, ne valide et ne demande jamais la clé dans le chat. Si le CLI ou la clé est indisponible, indique que la recherche Parallel est bloquée et demande une configuration locale; ne prétends pas avoir effectué la recherche.
- Vérifie les affirmations importantes auprès des pages sources primaires renvoyées par Parallel. L'ordre de pertinence Parallel n'est pas le classement Google; utilise `shipglows-serp` pour des instantanés Google et `shipglows-gsc` pour les performances Search Console.
- Ignore la recherche Web si la tâche est autonome ou si l'opérateur demande explicitement de ne pas faire de recherche externe.

## Configuration de build et d'exécution

Doppler : **NONE**. Ce moteur et ses commandes privées de calibration A0 n'ont besoin d'aucun environnement Doppler. Exécute directement CMake, CTest, les exécutables de diagnostic et les scripts d'échantillonnage. Ne les enveloppe pas dans `doppler run` et n'emprunte pas les secrets BeatGlows. Cette exception explicite du projet remplace la règle générale Doppler pour build/run.

Préfère `flutter run` pour le travail interactif de diagnostic Flutter.
