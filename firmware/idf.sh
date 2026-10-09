#!/usr/bin/env bash
# Esegue idf.py con l'ambiente ESP-IDF v5.5.2 attivato.
# Lo script di attivazione accetta di essere incluso solo se $0 è "bash",
# per questo lo includiamo dentro un bash -c con $0 impostato a "bash".
exec bash -c '. "$HOME/.espressif/tools/activate_idf_v5.5.2.sh" > /dev/null && exec python "$IDF_PATH/tools/idf.py" "$@"' bash "$@"
