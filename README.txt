Host-specific OpenFX bugs and caveats:

* DaVinci Resolve Lite

OFX API version 1.3
hostName=DaVinciResolveLite
hostLabel=DaVinci Resolve Lite
hostVersion=11.1.4 (11.1.4)

- doesn't support symbolic links in /Library/OFX/Plugins (on OS X)
- in Generators, even if the source clip is defined, it can not be fetched by the plug-in
- kOfxImagePropField property is always kOfxImageFieldNone on OFX images
- The display range has to be defined for all Double params (kOfxParamTypeDouble, kOfxParamTypeDouble2D, kOfxParamTypeDouble3D), or a default range of (-1,1) is used, and values cannot lie outsideof this range !
- kOfxParamPropDefaultCoordinateSystem=kOfxParamCoordinatesNormalised isn't supported (although API version 1.3 is claimed)
- kOfxParamTypeInteger2D kOfxParamTypeInteger3D are not supported (crash when opening the parameters page), at least in Generators

* Nuke

OFX API version 1.2
hostName=uk.co.thefoundry.nuke
hostLabel=nuke
hostVersion=9.0.1 (9.0v1)

- ChoiceParam items can only be set during description and cannot be changed afterwards
- Params that are described as secret can never be "revealed", they are doomed to remain secret (fix: set them as secret at the end of effect instance creation)
