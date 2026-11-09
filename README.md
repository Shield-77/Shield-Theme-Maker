# Shield Theme Maker

Application homebrew Nintendo 3DS (y compris old 3DS) pour fabriquer un thème du menu HOME directement sur la console. Le code a été généré avec Codex.

Version 0.1.3 : chaque écran se règle dans une étape distincte. L/R zoome et dézoome, la croix choisit la partie visible, et l'aperçu est affiché immédiatement. Le thème exporté inclut un `preview.png` pour Anemone3DS. Les musiques OGG et MP3 peuvent être converties directement sur la console.

## Installation CIA

Copier `shield_theme_maker.cia` sur la carte SD puis l'installer avec FBI sur une 3DS équipée de CFW. L'icône et la bannière apparaissent sur le menu HOME. Le splash de l'application est inclus dans la RomFS du CIA.

## Installation 3DSX (alternative)

Copier le contenu du dossier `3ds/shield_theme_maker` de l'archive de distribution dans le même dossier sur la carte SD, puis lancer `shield_theme_maker.3dsx` depuis Homebrew Launcher. Une console capable de lancer des homebrews est nécessaire.

## Utilisation

- Avec la croix directionnelle, parcourir la carte SD. **Y** prévisualise l'image surlignée sur l'écran supérieur ; **A** la choisit pour le haut ou le bas du thème.
- Choisir éventuellement un fichier `BCSTM`, `OGG` ou `MP3` pour la musique, ou appuyer sur **X** pour continuer sans musique. OGG/MP3 sont convertis sur la console en `bgm.bcstm` stéréo, à 16 kHz, avec une boucle de 44 secondes maximum.
- À l'étape 4, régler **uniquement l'image du haut**. Regarder l'écran du haut : le cadre bleu la désigne. Utiliser **L** pour dézoomer, **R** pour zoomer, et les flèches de la croix directionnelle pour faire apparaître davantage la partie voulue. **X** alterne entre « image entière » et « écran rempli ». Appuyer sur **A** pour passer à l'image du bas.
- À l'étape 5, faire la même chose pour **uniquement l'image du bas**, indiquée par un cadre jaune. **B** revient à l'image du haut ; **A** passe à la vérification.
- À l'étape 6, appuyer sur **A** pour créer le thème. Il sera enregistré automatiquement dans `sdmc:/themes/Shield_<nom>/`, le dossier lu par Anemone3DS, avec `body_LZ.bin`, `preview.png` et, si fournie, `bgm.bcstm`. Un suffixe numérique évite d'écraser un thème existant.
- **B** remonte dans les dossiers ou revient à l'étape précédente. **START** ferme l'application.

### Mode automatique

Placer `haut.png` ou `top.png`, et `bas.png` ou `bottom.png`, dans `sdmc:/3ds/ShieldThemeMaker/input/` (les extensions `.jpg` et `.jpeg` fonctionnent aussi). Ajouter éventuellement `bgm.bcstm`, `bgm.ogg` ou `bgm.mp3` au même endroit. À la première étape, appuyer sur **X** pour détecter les fichiers et préparer le thème. Le dossier d'entrée est créé au premier lancement. Les photos prises avec la console restent accessibles dans `sdmc:/DCIM/` via le navigateur intégré.

Anemone3DS peut ensuite appliquer le thème créé depuis sa liste. Shield Theme Maker exporte le thème et n'écrit pas directement dans les données du menu HOME.

## Limites actuelles

- Images PNG/JPEG jusqu'à 2,4 millions de pixels et 4096 pixels par côté, afin de limiter l'usage de mémoire sur old 3DS.
- Les BCSTM importés doivent avoir un en-tête `CSTM` et tenir dans 3 Mo. Les OGG/MP3 sont décodés au fil de l'eau et enregistrés en PCM16 stéréo dans un BCSTM de 44 secondes au maximum. Pour une piste plus longue, seule la première partie est conservée et bouclée ; le WAV n'est pas accepté.
- Fond statique, sans animation ni sons d'interface personnalisés.
- Le redimensionnement utilise une correspondance entière préparée une fois par axe, avec interpolation ou moyenne de 16 échantillons selon l'échelle. Les binaires sont compilés avec devkitPro ; la vitesse et le fonctionnement sur console physique restent à vérifier.

## Compilation

Installer devkitPro avec devkitARM et libctru, définir `DEVKITPRO` et `DEVKITARM`, puis exécuter `make` dans ce dossier. La compilation produit `shield_theme_maker.elf` et `shield_theme_maker.3dsx` et intègre `romfs/splash.png`. L'icône est générée depuis `icon.png`.

Pour produire le CIA, installer [makerom](https://github.com/3DSGuy/Project_CTR/releases) et [bannertool](https://github.com/carstene1ns/3ds-bannertool/releases), puis exécuter depuis ce dossier :

```sh
bannertool makebanner -i banner.png -a banner.wav -o banner.bnr
makerom -f cia -o shield_theme_maker.cia -DAPP_ENCRYPTED=false -rsf shield.rsf -target t -ver 4 -exefslogo -elf shield_theme_maker.elf -icon shield_theme_maker.smdh -banner banner.bnr
```

`shield.rsf` configure le mode Legacy compatible old 3DS. La bannière Homebrew Launcher est `shield_theme_maker-banner-fullscreen.png`.

## Sources techniques

- Format du thème : [3dbrew Home Menu/Themes](https://www.3dbrew.org/wiki/Home_Menu/Themes/).
- Outils et structure de référence : [Kame Tools](https://gitlab.com/beelzy/kame-tools).
- Décodeur PNG/JPEG : `source/stb_image.h`, de Sean Barrett, sous licence MIT ou domaine public selon le choix proposé dans son en-tête.
- Écriture PNG : `source/stbi_image_write.[ch]`, de Sean Barrett, domaine public.
- Décodage MP3 : `source/minimp3*.h`, de Lieff, CC0 (`LICENSE-minimp3.txt`). Décodage OGG : libvorbisidec/Tremor fournie par devkitPro.

Le visuel de l'application provient de l'image fournie pour ce projet : `https://files.catbox.moe/7rtp80.png`.
