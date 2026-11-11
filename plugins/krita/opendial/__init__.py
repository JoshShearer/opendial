# SPDX-License-Identifier: MIT
from krita import Krita

from .opendial import OpenDialExtension

Krita.instance().addExtension(OpenDialExtension(Krita.instance()))
