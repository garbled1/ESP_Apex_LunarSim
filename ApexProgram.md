# Program for Neptune Apex

## Outlet

### LED_Moon
	Fallback OFF 
	Set OFF 
	If Moon 120/120 Then ON 
	If Tmp > 82.0 Then OFF

## VDM

### BluLED_4_5 (your VDM Port)
    Fallback OFF
    If Output VMoonRise = ON Then ESPMoonRise
    If Output VMoonSet = ON Then ESPMoonSet
	If Output VMoonOn = ON Then ESPMoonUP
	If Output LED_Moon = OFF Then OFF

## Virtual Outlets

### VMoonRise
	Set OFF
	If Moon 120/-360 Then ON
	If Output VMoonOn = ON Then OFF
	
### VMoonSet
	Set OFF 
	If Moon 360/120 Then ON

### VMoonOn
	Set OFF 
	If Moon 180/060 Then ON
	
## Profiles

### ESPMoonRise
	<type>ramp</type>
	<rampTime>90</rampTime>
	<startIntensity>0</startIntensity>
	<endIntensity>100</endIntensity>

### ESPMoonUP
	<type>ramp</type>
	<rampTime>1</rampTime>
	<startIntensity>100</startIntensity>
	<endIntensity>100</endIntensity>

### ESPMoonSet
	<type>ramp</type>
	<rampTime>90</rampTime>
	<startIntensity>100</startIntensity>
	<endIntensity>0</endIntensity>
