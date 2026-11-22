"""Free Riders Recompiled for PS5 builder: the window.

Pick your Sonic Free Riders (Xbox 360) disc image and a folder, press Build.
The work is done by build.py (in this folder), run as a program of its own.
"""
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

HERE = Path(__file__).resolve().parent
WINDOWS = os.name == 'nt'
NAVY, CARD, TEAL, ORANGE, TEXT, SOFT = '#141830', '#1b2142', '#3be0c8', '#ff7a2f', '#ffffff', '#aab2d5'


def python_console():
    """python.exe beside pythonw.exe (the window runs under pythonw)."""
    exe = Path(sys.executable)
    if exe.name.lower() == 'pythonw.exe':
        candidate = exe.with_name('python.exe')
        if candidate.is_file():
            return str(candidate)
    return str(exe)


class App:
    def __init__(self, root):
        self.root = root
        self.process = None
        self.lines = queue.Queue()
        root.title('Free Riders Recompiled for PS5 - Builder')
        root.configure(bg=NAVY)
        root.minsize(640, 460)
        style = ttk.Style(root)
        try:
            style.theme_use('clam')
        except tk.TclError:
            pass
        style.configure('Bar.Horizontal.TProgressbar', troughcolor=CARD, background=ORANGE, bordercolor=NAVY,
                        lightcolor=ORANGE, darkcolor=ORANGE, thickness=16)

        def label(parent, text, size=10, color=TEXT, bold=False):
            return tk.Label(parent, text=text, bg=parent['bg'], fg=color,
                            font=('Segoe UI', size, 'bold' if bold else 'normal'), anchor='w', justify='left')

        head = tk.Frame(root, bg=NAVY)
        head.pack(fill='x', padx=24, pady=(20, 6))
        label(head, 'FRR · RECOMPILED FOR PS5', 10, TEAL, True).pack(anchor='w')
        label(head, 'Builder', 22, TEXT, True).pack(anchor='w')
        label(head, 'Makes the PS5 title and its game data from your own Sonic Free Riders (Xbox 360) disc image.\n'
                    'Nothing from the game is included in this builder.', 10, SOFT).pack(anchor='w', pady=(4, 0))

        form = tk.Frame(root, bg=CARD)
        form.pack(fill='x', padx=24, pady=10)
        self.disc = tk.StringVar()
        self.output = tk.StringVar(value=str(Path.home() / 'Desktop' / 'FRR-PS5'))
        for row, (title, var, pick) in enumerate((('Disc image (.iso)', self.disc, self.pick_disc),
                                                  ('Output folder', self.output, self.pick_output))):
            label(form, title, 10, SOFT).grid(row=row, column=0, sticky='w', padx=(14, 8), pady=8)
            entry = tk.Entry(form, textvariable=var, bg=NAVY, fg=TEXT, insertbackground=TEXT, relief='flat',
                             font=('Segoe UI', 10))
            entry.grid(row=row, column=1, sticky='ew', pady=8, ipady=4)
            tk.Button(form, text='Browse…', command=pick, bg=NAVY, fg=TEXT, activebackground=TEAL, relief='flat',
                      padx=10).grid(row=row, column=2, padx=(8, 14), pady=8)
        form.columnconfigure(1, weight=1)

        self.data = tk.BooleanVar(value=True)
        tk.Checkbutton(root, text='Also prepare the game data (frr-ps5 folder)', variable=self.data, bg=NAVY, fg=TEXT,
                       selectcolor=CARD, activebackground=NAVY, activeforeground=TEXT,
                       font=('Segoe UI', 10)).pack(anchor='w', padx=20)

        self.build_button = tk.Button(root, text='Build', command=self.start, bg=ORANGE, fg=TEXT,
                                      activebackground=TEAL, relief='flat', font=('Segoe UI', 12, 'bold'),
                                      padx=26, pady=6)
        self.build_button.pack(anchor='w', padx=24, pady=(12, 8))
        self.step = label(root, 'Ready.', 11, TEXT, True)
        self.step.pack(fill='x', padx=24)
        self.bar = ttk.Progressbar(root, style='Bar.Horizontal.TProgressbar', maximum=100)
        self.bar.pack(fill='x', padx=24, pady=8)
        self.note = label(root, 'The first build takes about 15 to 30 minutes, depending on your PC.', 9, SOFT)
        self.note.pack(fill='x', padx=24)
        root.protocol('WM_DELETE_WINDOW', self.close)
        root.after(100, self.poll)

    def pick_disc(self):
        path = filedialog.askopenfilename(title='Your Sonic Free Riders disc image',
                                          filetypes=[('Disc image', '*.iso'), ('All files', '*.*')])
        if path:
            self.disc.set(path)

    def pick_output(self):
        path = filedialog.askdirectory(title='Where to put PPSA48220 and frr-ps5')
        if path:
            self.output.set(path)

    def start(self):
        disc, output = self.disc.get().strip(), self.output.get().strip()
        if not disc or not Path(disc).exists():
            messagebox.showerror('Builder', 'Pick your disc image (.iso) first.')
            return
        if not output:
            messagebox.showerror('Builder', 'Pick an output folder.')
            return
        args = [python_console(), str(HERE / 'build.py'), disc, output]
        if not self.data.get():
            args.append('--skip-data')
        flags = subprocess.CREATE_NO_WINDOW if WINDOWS else 0
        self.process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                        creationflags=flags, cwd=str(HERE))
        self.build_button.configure(state='disabled', text='Building…')
        self.bar['value'] = 0
        self.last_error = ''
        threading.Thread(target=self.read, daemon=True).start()

    def read(self):
        for line in self.process.stdout:
            self.lines.put(line.rstrip('\n'))
        self.lines.put(None)

    def poll(self):
        try:
            while True:
                line = self.lines.get_nowait()
                if line is None:
                    self.finished()
                    break
                if line.startswith('STEP '):
                    number, _, text = line[5:].partition(' ')
                    self.step.configure(text=f'Step {number}: {text}')
                    # A moving bar until the step reports how far along it is.
                    self.bar.stop()
                    self.bar.configure(mode='indeterminate')
                    self.bar.start(12)
                elif line.startswith('PERCENT '):
                    if str(self.bar['mode']) != 'determinate':
                        self.bar.stop()
                        self.bar.configure(mode='determinate')
                    self.bar['value'] = float(line.split()[1])
                elif line.startswith('DONE '):
                    self.done_minutes = line.split()[1]
                elif line.strip():
                    self.last_error = line
        except queue.Empty:
            pass
        self.root.after(100, self.poll)

    def finished(self):
        code = self.process.wait()
        self.process = None
        self.bar.stop()
        self.bar.configure(mode='determinate')
        self.build_button.configure(state='normal', text='Build')
        output = Path(self.output.get())
        if code == 0:
            self.bar['value'] = 100
            self.step.configure(text='Done.')
            messagebox.showinfo('Builder', f'Finished in {getattr(self, "done_minutes", "?")} minutes.\n\n'
                                           f'Install {output / "PPSA48220"} as a homebrew title, and copy '
                                           f'{output / "frr-ps5"} to /data/frr-ps5 on the console.')
            if WINDOWS:
                os.startfile(output)
        else:
            self.step.configure(text='The build stopped.')
            messagebox.showerror('Builder', f'The build stopped:\n\n{self.last_error}\n\n'
                                            f'Details are in {output / "frr-build.log"}.')

    def close(self):
        if self.process and messagebox.askyesno('Builder', 'A build is running. Stop it and close?'):
            self.process.kill()
        elif self.process:
            return
        self.root.destroy()


def main():
    root = tk.Tk()
    App(root)
    root.mainloop()


if __name__ == '__main__':
    main()
