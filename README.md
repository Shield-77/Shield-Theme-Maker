# Shield Theme Maker

Application homebrew Nintendo 3DS (y compris old 3DS) pour fabriquer un thème du menu HOME directement sur la console. Le code a été généré avec Codex.

Version 0.1.2 : le cadrage automatique préserve l'image entière quand un recadrage couperait trop de contenu. Le redimensionnement mélange plusieurs pixels pour éviter les bords crénelés. On peut déplacer précisément la partie conservée de chaque image et l'aperçu utilise le même cadrage que l'export.

## Installation CIA

Copier `shield_theme_maker.cia` sur la carte SD puis l'installer avec FBI sur une 3DS équipée de CFW. L'icône et la bannière apparaissent sur le menu HOME. Le splash de l'application est inclus dans la RomFS du CIA.

## Installation 3DSX (alternative)

Copier le contenu du dossier `3ds/shield_theme_maker` de l'archive de distribution dans le même dossier sur la carte SD, puis lancer `shield_theme_maker.3dsx` depuis Homebrew Launcher. Une console capable de lancer des homebrews est nécessaire.

## Utilisation

- Avec la croix directionnelle, parcourir la carte SD. **Y** prévisualise l'image surlignée sur l'écran supérieur ; **A** la choisit pour le haut ou le bas du thème.
- Choisir éventuellement un fichier `BCSTM` pour la musique, ou appuyer sur **X** pour continuer sans musique.
- Avant la création, **X** alterne entre trois modes : automatique intelligent, image entière avec marges sombres, et recadrage centré. **Y** prévisualise l'image choisie avec ce cadrage.
- Sur l'écran « Prêt à créer », **L/R** choisit l'image du haut ou du bas. La croix déplace la zone conservée par pas de 10 % ; elle active automatiquement le recadrage. L'aperçu se met à jour après chaque déplacement, et **Y** permet de le revoir. Les deux images gardent chacune leur propre réglage.
- Appuyer sur **A** pour créer le thème. Il sera enregistré automatiquement dans `sdmc:/themes/Shield_<nom>/`, le dossier lu par Anemone3DS, avec `body_LZ.bin` et, si fournie, `bgm.bcstm`. Un suffixe numérique évite d'écraser un thème existant.
- **B** remonte dans les dossiers ou revient à l'étape précédente. **START** ferme l'application.

### Mode automatique

Placer `haut.png` ou `top.png`, et `bas.png` ou `bottom.png`, dans `sdmc:/3ds/ShieldThemeMaker/input/` (les extensions `.jpg` et `.jpeg` fonctionnent aussi). Ajouter éventuellement `bgm.bcstm` au même endroit. À la première étape, appuyer sur **X** pour détecter les fichiers et préparer le thème. Le dossier d'entrée est créé au premier lancement. Les photos prises avec la console restent accessibles dans `sdmc:/DCIM/` via le navigateur intégré.

Anemone3DS peut ensuite appliquer le thème créé depuis sa liste. Shield Theme Maker exporte le thème et n'écrit pas directement dans les données du menu HOME.

## Limites actuelles

- Images PNG/JPEG jusqu'à 2,4 millions de pixels et 4096 pixels par côté, afin de limiter l'usage de mémoire sur old 3DS.
- Musique au format `BCSTM` uniquement, avec en-tête `CSTM` et taille maximale de 3 Mo. La conversion MP3/WAV n'est pas incluse.
- Fond statique, sans animation ni sons d'interface personnalisés.
- Le redimensionnement utilise une correspondance entière préparée une fois par axe. Le mode automatique recadre les différences de proportions modestes et conserve l'image entière si le recadrage dépasserait environ 12 %. Les binaires sont compilés avec devkitPro ; la vitesse et le fonctionnement sur console physique restent à vérifier.

## Compilation

Installer devkitPro avec devkitARM et libctru, définir `DEVKITPRO` et `DEVKITARM`, puis exécuter `make` dans ce dossier. La compilation produit `shield_theme_maker.elf` et `shield_theme_maker.3dsx` et intègre `romfs/splash.png`. L'icône est générée depuis `icon.png`.

Pour produire le CIA, installer [makerom](https://github.com/3DSGuy/Project_CTR/releases) et [bannertool](https://github.com/carstene1ns/3ds-bannertool/releases), puis exécuter depuis ce dossier :

```sh
bannertool makebanner -i banner.png -a banner.wav -o banner.bnr
makerom -f cia -o shield_theme_maker.cia -DAPP_ENCRYPTED=false -rsf shield.rsf -target t -ver 3 -exefslogo -elf shield_theme_maker.elf -icon shield_theme_maker.smdh -banner banner.bnr
```

`shield.rsf` configure le mode Legacy compatible old 3DS. La bannière Homebrew Launcher est `shield_theme_maker-banner-fullscreen.png`.

## Sources techniques

- Format du thème : [3dbrew Home Menu/Themes](https://www.3dbrew.org/wiki/Home_Menu/Themes/).
- Outils et structure de référence : [Kame Tools](https://gitlab.com/beelzy/kame-tools).
- Décodeur PNG/JPEG : `source/stb_image.h`, de Sean Barrett, sous licence MIT ou domaine public selon le choix proposé dans son en-tête.

Le visuel de l'application provient de l'image fournie pour ce projet : `https://files.catbox.moe/7rtp80.png`.
