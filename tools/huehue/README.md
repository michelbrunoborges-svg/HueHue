# Arte do HueHue Switch

- `huehue-face.png`: a cara do HueHue recortada (fundo transparente).
- `make_assets.py`: gera os ícones e a splash do app em `dist/uwp/Assets`.

```
python3 tools/huehue/make_assets.py dist/uwp/Assets tools/huehue/huehue-face.png
```

Precisa de Pillow e da fonte Poppins Bold (caminho em `FONT` no script).
