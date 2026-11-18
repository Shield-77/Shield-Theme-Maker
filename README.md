Shield Theme Maker

**Codex was used to generate the code**


Créez vos propres thèmes 3DS directement sur votre console, sans ordinateur !

Shield Theme Maker est une application homebrew qui permet de créer facilement des thèmes personnalisés pour 3DS et 2DS, avec une attention particulière portée aux performances des modèles Old 3DS.

Le principe est simple : choisissez une image pour l'écran supérieur, une autre pour l'écran inférieur et, si vous le souhaitez, ajoutez votre musique préférée.

Pas besoin de préparer les images sur ordinateur, de comprendre les formats des thèmes Nintendo ou de convertir manuellement vos musiques en BCSTM. L'application s'occupe de la partie technique directement sur votre console.

Vous gardez également le contrôle sur le résultat : cadrage des images, zoom, choix de l'icône, passage musical, qualité audio et transition de boucle.

Une fois terminé, votre thème est automatiquement exporté dans le bon dossier de la carte SD, prêt à être utilisé avec Anemone3DS.

Codex a été utilisé pour générer le code. J'ai participé à la conception de l'application, au choix des fonctionnalités, aux tests sur console et aux corrections apportées au fil du développement.

Le but est simplement de rendre la création de thèmes 3DS accessible au plus grand nombre, même aux personnes qui n'y connaissent rien.

Quelques exemples de mes créations

Voici quelques thèmes que j'ai personnellement réalisés avec Shield Theme Maker, directement sur ma Old 3DS.

Les images utilisées proviennent de Pinterest. Je précise que ces thèmes sont des créations personnelles et que je n'ai pas distribué leurs archives.

Les illustrations appartiennent à leurs artistes respectifs. Je ne suis pas l'auteur de ces magnifiques images et je ne revendique aucun droit dessus.

<img width="440" alt="Exemples de thèmes créés avec Shield Theme Maker" src="https://github.com/user-attachments/assets/b244eb11-e0ec-4958-aa7a-1eaaabfc365e" />Tous ces thèmes ont été testés plusieurs fois sur ma console. Les images sont correctement cadrées, les icônes sont reconnues par Anemone3DS et les musiques fonctionnent, y compris leurs boucles.

C'est aussi pour ça que j'ai créé cette application : pouvoir utiliser les images et les musiques que j'aime sans devoir passer des heures à comprendre des outils compliqués sur PC.

Télécharger Shield Theme Maker

Version actuelle : "v0.2.11" (https://github.com/Shield-77/Shield-Theme-Maker/releases/tag/v0.2.11)

- "Télécharger le CIA" (https://github.com/Shield-77/Shield-Theme-Maker/releases/download/v0.2.11/shield_theme_maker.cia)
- "Parcourir le code source" (source/)
- "Télécharger l'archive source" (https://github.com/Shield-77/Shield-Theme-Maker/releases/download/v0.2.11/Shield-Theme-Maker-v0.2.11-source.zip)

<img width="440" alt="Shield Theme Maker sur Nintendo 3DS" src="https://github.com/user-attachments/assets/4c805811-56fd-41cb-a0cf-9ab95b54454c" />Comment créer un thème ?

1. Premier lancement

Au premier démarrage, l'application vous propose de choisir votre langue et affiche un guide de huit pages pour comprendre son fonctionnement.

Ce guide reste accessible à tout moment dans les options avec SELECT → Guide.

Le menu SELECT permet également de modifier la langue.

2. Choisir les images

Parcourez votre carte SD et sélectionnez une image pour l'écran supérieur, puis une autre pour l'écran inférieur.

Les images peuvent provenir de n'importe quelle source, à condition d'être dans un format compatible.

Formats acceptés :

- PNG
- JPEG

Les images sont acceptées jusqu'à 2,4 millions de pixels, avec une limite de 4096 pixels par côté.

Les dossiers apparaissent en premier, suivis des fichiers classés du plus récent au plus ancien.

Commandes :

- Haut/Bas : naviguer.
- Y : prévisualiser une image.
- A : sélectionner une image.
- B : revenir en arrière.

Pas besoin de redimensionner manuellement chaque image sur PC avant de commencer.

3. Ajuster le cadrage

C'est une des fonctionnalités que je tenais particulièrement à intégrer.

Toutes les images n'ont pas les mêmes dimensions, et certaines peuvent être mal cadrées lorsqu'elles sont directement utilisées comme fond d'écran.

Shield Theme Maker permet donc d'ajuster séparément les images des deux écrans.

Commandes :

- Croix directionnelle : déplacer l'image.
- L/R : modifier le zoom.
- X : changer de mode d'affichage.
- Y : proposer un centrage automatique.

Trois modes sont disponibles :

- Remplir : l'image remplit entièrement l'écran.
- Entière : l'image reste entièrement visible.
- Prolonger : les pixels des bords sont répétés pour remplir l'espace restant, sans ajouter de flou.

Le cadrage automatique est là pour vous faciliter la tâche, mais vous pouvez toujours ajuster manuellement le résultat.

4. Ajouter une musique (facultatif)

Vous pouvez ajouter votre musique préférée directement depuis la carte SD.

Formats acceptés :

- MP3
- OGG
- BCSTM

L'application permet de préécouter la musique avant de l'intégrer.

Commandes :

- Y : démarrer ou arrêter la préécoute.
- B : arrêter également la préécoute.

Vous pouvez ensuite choisir :

- Le passage de la musique que vous souhaitez utiliser.
- La qualité audio.
- La transition de boucle.

Par exemple, si vous aimez particulièrement un passage d'une musique, vous pouvez sélectionner cette partie pour l'utiliser dans votre thème.

La transition permet d'obtenir une répétition plus naturelle. Le résultat dépend évidemment du morceau et des réglages choisis.

Et si vous ne voulez pas de musique, aucun problème : les thèmes sans audio sont également pris en charge.

5. Choisir un nom et une icône

Vous pouvez donner un nom personnalisé à votre thème.

Si vous avez la flemme de trouver un nom, vous pouvez également conserver celui généré automatiquement. Personnellement, je reconnais surtout mes thèmes grâce à leurs icônes !

L'application permet justement de choisir l'image utilisée comme icône :

- L'image de l'écran supérieur.
- L'image de l'écran inférieur.

Cela permet de retrouver plus facilement vos créations dans Anemone3DS.

Si un thème porte déjà le même nom, un suffixe est automatiquement ajouté pour éviter d'écraser le thème existant.

6. Exporter le thème

Une fois votre création terminée, vous pouvez l'exporter sous forme de dossier ou d'archive ZIP.

Les thèmes sont automatiquement enregistrés dans :

"sdmc:/themes/"

Vous n'avez donc pas besoin de déplacer manuellement les fichiers après la création.

Un thème comprend notamment :

- "body_LZ.bin"
- "preview.png"
- "icon.png"
- "info.smdh"
- "bgm.bcstm" si une musique est ajoutée.

Un fichier "bgm.ogg" peut également être conservé lorsque la musique d'origine est au format OGG.

Si vous importez un MP3, aucun fichier OGG supplémentaire n'est généré.

Le fichier "bgm.bcstm" suffit pour la musique du thème.

Il ne reste ensuite qu'à ouvrir Anemone3DS pour appliquer votre création.

Shield Theme Maker ne modifie pas directement les données du menu HOME.

Musique : qualité et durée

La conversion des fichiers MP3 et OGG est effectuée directement sur la console en PCM16 stéréo.

La 3DS impose certaines limites concernant la taille des musiques utilisées dans les thèmes.

C'est pourquoi Shield Theme Maker propose différentes fréquences d'échantillonnage.

Une fréquence élevée permet de conserver davantage de détails audio, mais réduit la durée maximale du morceau.

À l'inverse, une fréquence plus faible permet d'utiliser un passage plus long, avec une qualité sonore réduite.

La fréquence de 16 kHz est sélectionnée par défaut.

Fréquence| Durée maximale
8 kHz| 1 min 45 s
10 kHz| 1 min 24 s
12 kHz| 1 min 10 s
14 kHz| 1 min
16 kHz| 52 s
18 kHz| 46 s
20 kHz| 41 s

Personnellement, j'ai obtenu de très bons résultats avec certaines musiques de Splatoon, notamment à 14 et 16 kHz.

La qualité dépend beaucoup du morceau. Certaines musiques supportent mieux une fréquence réduite que d'autres.

Les boucles musicales

La gestion de l'audio a demandé pas mal de corrections pendant le développement.

Les premières versions pouvaient produire des thèmes dont la musique ne fonctionnait pas correctement.

La version v0.2.11 vérifie désormais le fichier BCSTM avant l'exportation et corrige notamment un problème de répétition du début après une transition en fondu.

J'ai personnellement réalisé plusieurs thèmes avec musique et effectué de nombreux tests sur ma Old 3DS.

J'ai notamment réussi à créer des boucles très propres sur certaines musiques, sans coupure audible lors de la répétition.

Cela ne veut pas dire que tous les fichiers audio fonctionneront forcément parfaitement.

Si vous rencontrez un problème, essayez de créer le même thème sans musique pour vérifier si le problème vient de l'audio.

Vous pouvez également signaler le fichier utilisé et les réglages sélectionnés.

Limitations techniques

Les fichiers BCSTM importés sont vérifiés et doivent respecter la limite de "0x337000" octets imposée au thème.

Quelques limitations sont également à connaître :

- Les fichiers WAV ne sont pas acceptés comme musique d'entrée.
- Les fonds d'écran sont statiques.
- Les fonds animés ne sont pas pris en charge.
- Les sons d'interface personnalisés ne sont pas pris en charge.

Les thèmes sans musique ont également bénéficié de nombreuses corrections concernant le cadrage et la génération des fichiers.

À propos d'Anemone3DS

Je précise que je ne suis pas affilié au projet Anemone3DS.

Shield Theme Maker permet de créer les thèmes, mais leur application sur le menu HOME nécessite un outil comme Anemone3DS.

Concernant les aperçus, les fichiers "preview.png" générés par Shield Theme Maker fonctionnent correctement dans mes tests.

Les musiques de mes thèmes fonctionnent également une fois appliquées sur le menu HOME.

En revanche, j'ai rencontré des problèmes avec la préécoute audio dans certaines versions d'Anemone3DS.

Je parle bien de la préécoute dans l'application, pas forcément de la musique du thème une fois installé.

De mon côté, j'utilise une version d'Anemone qui fonctionne correctement pour mes besoins.

Il existe aussi des projets qui ont repris Anemone et y ont apporté différentes améliorations.

Certains utilisent l'IA pour développer une partie de leur code. Personnellement, je ne suis pas contre, puisque j'utilise moi-même Codex pour mes projets.

Mais je sais que certaines personnes n'aiment pas les projets développés de cette manière, surtout lorsqu'ils touchent à des données sensibles de la console.

Et je peux comprendre cette prudence.

Cela dit, utiliser une IA ne signifie pas automatiquement qu'un programme est dangereux. Ce qui compte, c'est aussi la qualité du code, les tests effectués et la manière dont l'application manipule les données.

Je préfère quand même rappeler qu'un thème mal appliqué peut parfois provoquer des problèmes avec le menu HOME.

Dans certains cas, le fond d'écran peut simplement ne pas s'afficher correctement. Dans d'autres, le menu HOME peut rencontrer des problèmes de démarrage jusqu'à ce que les données de thèmes concernées soient corrigées ou supprimées.

Ce n'est pas forcément un brick définitif de la console, mais cela reste un problème à prendre au sérieux.

Je préfère donc prévenir plutôt que de faire comme si aucun risque n'existait.

Votre vie, vos choix, pas les miens. Profitez bien !

Installer ou compiler

La release officielle contient le fichier :

"shield_theme_maker.cia"

Si un fichier 3DSX est fourni dans une release, vous pouvez le placer dans :

"sdmc:/3ds/shield_theme_maker/"

Il pourra alors être utilisé avec le Homebrew Launcher.

Vérifiez les fichiers disponibles dans la release que vous souhaitez télécharger.

Compilation depuis le code source

Pour compiler Shield Theme Maker, installez :

- devkitPro / devkitARM.
- Les dépendances 3DS indiquées dans le Makefile.
- "makerom" et "bannertool", à obtenir séparément pour générer un CIA.

La compilation peut être lancée avec :

make

Le code source public utilise la version CIA interne 1025.

Droits et crédits

Le code propre à Shield Theme Maker est distribué sous licence "GNU GPLv3" (LICENSE).

Les composants tiers conservent leurs propres licences :

- "Atkinson Hyperlegible : SIL OFL" (LICENSE-AtkinsonHyperlegible.txt)
- "minimp3 : CC0" (LICENSE-minimp3.txt)
- stb_image.
- Les bibliothèques audio de devkitPro.

Les images, illustrations et musiques utilisées par les utilisateurs restent soumises aux droits de leurs auteurs respectifs.

Le nom SHIELD et son identité visuelle permettent d'identifier les publications officielles de Shield-77.

Si vous modifiez le projet ou créez votre propre version, merci d'indiquer clairement qu'il s'agit d'une version modifiée et de ne pas la présenter comme une publication officielle.

Merci à toutes les personnes qui s'intéressent à mes projets.

SHIELD est un ensemble de projets homebrew gratuits que je développe avec l'aide de Codex, en participant à leur conception et en effectuant les tests directement sur Nintendo 3DS.

Le but est simplement de partager des applications utiles et de continuer à faire vivre ces consoles.

Profitez bien de votre 3DS !

Shield-77
