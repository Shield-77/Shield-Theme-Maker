# Shield Theme Maker

Application homebrew Nintendo 3DS (y compris old 3DS) pour fabriquer un thème du menu HOME directement sur la console. Le code a été généré avec Codex.

Version 0.1.1 : les images PNG/JPEG présentes directement dans `sdmc:/` apparaissent dans le navigateur, Y prévisualise le fichier surligné, le redimensionnement fait moins de calculs sur old 3DS, et les thèmes créés sont placés directement dans le dossier d'Anemone3DS.

## Installation CIA

Copier `shield_theme_maker.cia` sur la carte SD puis l'installer avec FBI sur une 3DS équipée de CFW. L'icône et la bannière apparaissent sur le menu HOME. Le splash de l'application est inclus dans la RomFS du CIA.

## Installation 3DSX (alternative)

Copier le contenu du dossier `3ds/shield_theme_maker` de l'archive de distribution dans le même dossier sur la carte SD, puis lancer `shield_theme_maker.3dsx` depuis Homebrew Launcher. Une console capable de lancer des homebrews est nécessaire.

## Utilisation

- Avec la croix directionnelle, parcourir la carte SD. **Y** prévisualise l'image surlignée sur l'écran supérieur ; **A** la choisit pour le haut ou le bas du thème.
- Choisir éventuellement un fichier `BCSTM` pour la musique, ou appuyer sur **X** pour continuer sans musique.
- Avant la création, **X** alterne entre le recadrage central automatique et l'affichage de l'image entière avec des marges sombres. **Y** prévisualise l'image du bas avec ce cadrage.
- Appuyer sur **A** pour créer le thème. Il sera enregistré automatiquement dans `sdmc:/themes/Shield_<nom>/`, le dossier lu par Anemone3DS, avec `body_LZ.bin` et, si fournie, `bgm.bcstm`. Un suffixe numérique évite d'écraser un thème existant.
- **B** remonte dans les dossiers ou revient à l'étape précédente. **START** ferme l'application.

### Mode automatique

Placer `haut.png` ou `top.png`, et `bas.png` ou `bottom.png`, dans `sdmc:/3ds/ShieldThemeMaker/input/` (les extensions `.jpg` et `.jpeg` fonctionnent aussi). Ajouter éventuellement `bgm.bcstm` au même endroit. À la première étape, appuyer sur **X** pour détecter les fichiers et préparer le thème. Le dossier d'entrée est créé au premier lancement. Les photos prises avec la console restent accessibles dans `sdmc:/DCIM/` via le navigateur intégré.

Anemone3DS peut ensuite appliquer le thème créé depuis sa liste. Shield Theme Maker exporte le thème et n'écrit pas directement dans les données du menu HOME.

## Limites actuelles

- Images PNG/JPEG jusqu'à 2,4 millions de pixels et 4096 pixels par côté, afin de limiter l'usage de mémoire sur old 3DS.
- Musique au format `BCSTM` uniquement, avec en-tête `CSTM` et taille maximale de 3 Mo. La conversion MP3/WAV n'est pas incluse.
- Fond statique, sans animation ni sons d'interface personnalisés.
- Le redimensionnement utilise désormais une correspondance entière préparée une fois par axe, pour réduire les calculs sur old 3DS. Les binaires sont compilés avec devkitPro ; la vitesse et le fonctionnement sur console physique restent à vérifier.

## Compilation

Installer devkitPro avec devkitARM et libctru, définir `DEVKITPRO` et `DEVKITARM`, puis exécuter `make` dans ce dossier. La compilation produit `shield_theme_maker.elf` et `shield_theme_maker.3dsx` et intègre `romfs/splash.png`. L'icône est générée depuis `icon.png`.

Pour produire le CIA, installer [makerom](https://github.com/3DSGuy/Project_CTR/releases) et [bannertool](https://github.com/carstene1ns/3ds-bannertool/releases), puis exécuter depuis ce dossier :

```sh
bannertool makebanner -i banner.png -a banner.wav -o banner.bnr
makerom -f cia -o shield_theme_maker.cia -DAPP_ENCRYPTED=false -rsf shield.rsf -target t -ver 2 -exefslogo -elf shield_theme_maker.elf -icon shield_theme_maker.smdh -banner banner.bnr
```

`shield.rsf` configure le mode Legacy compatible old 3DS. La bannière Homebrew Launcher est `shield_theme_maker-banner-fullscreen.png`.

## Sources techniques

- Format du thème : [3dbrew Home Menu/Themes](https://www.3dbrew.org/wiki/Home_Menu/Themes/).
- Outils et structure de référence : [Kame Tools](https://gitlab.com/beelzy/kame-tools).
- Décodeur PNG/JPEG : `source/stb_image.h`, de Sean Barrett, sous licence MIT ou domaine public selon le choix proposé dans son en-tête.

Le visuel de l'application provient de l'image fournie pour ce projet : `https://files.catbox.moe/7rtp80.png`.
