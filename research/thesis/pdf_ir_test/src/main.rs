use lopdf::Document;
use std::env;

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        eprintln!("Usage: {} <input.pdf>", args[0]);
        std::process::exit(1);
    }

    let input_path  = &args[1];
    let output_path = "output.pdf";

    // === STEP A: bytes -> IR ===
    println!("=== STEP A: Parse PDF into IR ===");

    let mut doc = Document::load(input_path)
        .expect("Failed to load PDF file");

    println!("Version     : {}", doc.version);
    println!("Object count: {}", doc.objects.len());

    println!("\n--- Trailer ---");
    for (key, val) in &doc.trailer {
        // key is Vec<u8>, convert to string for display
        println!("  /{} = {:?}", String::from_utf8_lossy(key), val);
    }

    println!("\n--- First 5 objects ---");
    let mut count = 0;
    for (id, object) in &doc.objects {
        if count >= 5 { break; }
        println!("  Object {:?} = {:?}", id, object);
        count += 1;
    }

    println!("\n--- Catalog ---");
    match doc.catalog() {
        Ok(catalog) => {
            // catalog is already &Dictionary, iterate directly
            for (key, val) in catalog.iter() {
                println!("  /{} = {:?}", String::from_utf8_lossy(key), val);
            }
        }
        Err(e) => println!("  Error: {:?}", e),
    }

    println!("\nPage count: {}", doc.get_pages().len());

    // === STEP B: IR -> bytes ===
    println!("\n=== STEP B: Serialize IR to file ===");

    doc.save(output_path)
        .expect("Failed to save PDF file");

    println!("Saved to: {}", output_path);

    // === STEP C: Round-trip check ===
    println!("\n=== STEP C: Round-trip check ===");

    let doc2 = Document::load(output_path)
        .expect("Failed to load output file");

    let ok_version = doc.version == doc2.version;
    let ok_objects = doc.objects.len() == doc2.objects.len();
    let ok_pages   = doc.get_pages().len() == doc2.get_pages().len();

    println!("Version match : {}", if ok_version { "PASS" } else { "FAIL" });
    println!("Objects match : {} ({} == {})",
        if ok_objects { "PASS" } else { "FAIL" },
        doc.objects.len(), doc2.objects.len());
    println!("Pages match   : {} ({} == {})",
        if ok_pages { "PASS" } else { "FAIL" },
        doc.get_pages().len(), doc2.get_pages().len());

    if ok_version && ok_objects && ok_pages {
        println!("\nRound-trip OK");
    } else {
        println!("\nRound-trip FAILED");
        std::process::exit(1);
    }
}