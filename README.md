# nbwmon
ncurses bandwidth monitor

## Features
* print transmitted and received bandwidth graphs
* print current, maximum, average and minimum transfer speeds
* print total traffic received and transmitted
* scale to full window on startup and resize
* detect active network interface
* support for multiple units
* support for colors

## Runtime Options
| key | action                |
| :-: | :---                  |
| s   | use SI / IEC units    |
| b   | use Byte/s / Bit/s    |
| m   | scale graph minimum   |
| g   | show global maximum   |
| +   | increase redraw delay |
| -   | decrease redraw delay |

## Options
| option       |                            |
| :---         | :---                       |
| -h           | show help                  |
| -v           | show version               |
| -C           | disable colors             |
| -s           | use SI units               |
| -b           | use Bit/s                  |
| -m           | scale graph minimum        |
| -g           | show global maximum        |
| -d seconds   | redraw delay in  seconds   |
| -i interface | select   network interface |
