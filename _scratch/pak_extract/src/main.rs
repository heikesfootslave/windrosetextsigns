use std::{env, fs, path::Path};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = env::args().collect();
    if args.len() < 4 {
        eprintln!("usage: pak_extract <pak> <output-dir> <entry> [entry...]");
        std::process::exit(2);
    }

    let pak_path = &args[1];
    let output_dir = Path::new(&args[2]);
    let pak = unpak::Pak::new_any(pak_path, None)?;
    println!("mount_point={}", pak.mount_point());
    println!("version={:?}", pak.version());

    let entries = pak.entries();
    for requested in &args[3..] {
        let entry = entries
            .iter()
            .find(|entry| entry.eq_ignore_ascii_case(requested))
            .ok_or_else(|| format!("missing pak entry: {requested}"))?;

        let output_path = output_dir.join(entry);
        if let Some(parent) = output_path.parent() {
            fs::create_dir_all(parent)?;
        }
        pak.read_to_file(entry, &output_path)?;
        println!("{}", output_path.display());
    }

    Ok(())
}
