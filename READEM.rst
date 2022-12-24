==============================
Pulseaudio monitor and control
==============================

This cli application can monitor volume changes and set volume on master sink
/ source.

It's used to control volume from my `Awesome WM config
<https://github.com/mireq/awesome-wm-config>`_.

How it works
------------

To compile just run ``make`` inside current directory. If something is missing,
compiler will complain.

Then you can run application with ``./pulsectrl`` command. It will write events
to stdout and wait for commands on stdin. Command arguments are strictly
separated with one space. Monitor fields are strictly separated with tab
(``'\t'``) character.

Monitor events
--------------

Changed default sink / source to ``<name>``::

   default <sink|source>	<name>

Changed volume of sink / source to ``<name>`` to ``<volume>``::

   volume <sink|source> <flags>	<volume>	<name>

Flags is 2-character string (always 2 characters). First is default flag. If
source / sink is default, it will contain ``'*'``. If it's not default,  it will
contain space (``' '``). Second flag is mute, if sink / source is muted, it will
contain ``'M'``. If it's not, it will be one space character.

Control commands
----------------

Command format is::

   <target> <operation> [<operand>]

Operand ``<target>`` is sink or source.

Operations are:

- ``mute_toggle``
- ``mute_set``
- ``mute_clear``
- ``set <volume>``
- ``change [-]<volume>``

Value of ``<volume>`` is float point number from 0.0 to 1.0;

Change supports negative numbers, for example sink change -0.1 will decrease
volume by 10%.

Examples::

   # toggle default speaker mute
   sink mute_toggle

   # mute default microphone
   source mute_set

   # set default microphone volume to 80%
   source set 0.8

   # decrease speaker volume by 10%
   sink change -0.1
